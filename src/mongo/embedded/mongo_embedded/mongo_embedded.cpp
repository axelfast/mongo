/**
 *    Copyright (C) 2018-present MongerDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongerDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongerdb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */
#include "monger/platform/basic.h"

#include "monger_embedded/monger_embedded.h"

#include <cstring>
#include <exception>
#include <thread>
#include <unordered_map>
#include <vector>

#include "api_common.h"
#include "monger/db/client.h"
#include "monger/db/service_context.h"
#include "monger/embedded/embedded.h"
#include "monger/embedded/embedded_log_appender.h"
#include "monger/logger/logger.h"
#include "monger/logger/message_event_utf8_encoder.h"
#include "monger/rpc/message.h"
#include "monger/stdx/unordered_map.h"
#include "monger/transport/service_entry_point.h"
#include "monger/transport/transport_layer_mock.h"
#include "monger/util/assert_util.h"
#include "monger/util/scopeguard.h"
#include "monger/util/shared_buffer.h"

#if defined(_WIN32)
#define MONGO_API_CALL __cdecl
#else
#define MONGO_API_CALL
#endif

namespace monger {
using MongerEmbeddedStatusImpl = StatusForAPI<monger_embedded_v1_error>;

/**
 * C interfaces that use enterCXX() must provide a translateException() function that converts any
 * possible exception into a StatusForAPI<> object.
 */
static MongerEmbeddedStatusImpl translateException(
    stdx::type_identity<MongerEmbeddedStatusImpl>) try {
    throw;
} catch (const ExceptionFor<ErrorCodes::ReentrancyNotAllowed>& ex) {
    return {MONGO_EMBEDDED_V1_ERROR_REENTRANCY_NOT_ALLOWED, ex.code(), ex.what()};
} catch (const DBException& ex) {
    return {MONGO_EMBEDDED_V1_ERROR_EXCEPTION, ex.code(), ex.what()};
} catch (const ExceptionForAPI<monger_embedded_v1_error>& ex) {
    return {ex.statusCode(), monger::ErrorCodes::InternalError, ex.what()};
} catch (const std::bad_alloc& ex) {
    return {MONGO_EMBEDDED_V1_ERROR_ENOMEM, monger::ErrorCodes::InternalError, ex.what()};
} catch (const std::exception& ex) {
    return {MONGO_EMBEDDED_V1_ERROR_UNKNOWN, monger::ErrorCodes::InternalError, ex.what()};
} catch (...) {
    return {MONGO_EMBEDDED_V1_ERROR_UNKNOWN,
            monger::ErrorCodes::InternalError,
            "Unknown error encountered in performing requested stitch_support_v1 operation"};
}

/**
 * C interfaces that use enterCXX() must provide a tranlsateExceptionFallback() function that
 * populates a StatusForAPI<> object to indicate a double-fault error during error reporting. The
 * translateExceptionFallback() function gets called when translateException() throws, and it should
 * not include any code that may itself throw.
 *
 * We use an out param instead of returning the StatusForAPI<> object so as to avoid a std::string
 * copy that may allocate memory.
 */
static void translateExceptionFallback(MongerEmbeddedStatusImpl& status) noexcept {
    status.error = MONGO_EMBEDDED_V1_ERROR_IN_REPORTING_ERROR;
    status.exception_code = -1;
    setErrorMessageNoAlloc(status.what);
}
}  // namespace monger

struct monger_embedded_v1_status {
    monger::MongerEmbeddedStatusImpl statusImpl;
};

struct monger_embedded_v1_lib {
    ~monger_embedded_v1_lib() {
        invariant(this->databaseCount.load() == 0);

        if (this->logCallbackHandle) {
            using monger::logger::globalLogDomain;
            globalLogDomain()->detachAppender(this->logCallbackHandle);
            this->logCallbackHandle.reset();
        }
    }

    monger_embedded_v1_lib(const monger_embedded_v1_lib&) = delete;
    void operator=(const monger_embedded_v1_lib) = delete;

    monger_embedded_v1_lib() = default;

    monger::AtomicWord<int> databaseCount;

    monger::logger::ComponentMessageLogDomain::AppenderHandle logCallbackHandle;

    std::unique_ptr<monger_embedded_v1_instance> onlyDB;
};

namespace monger {
namespace {
MongerEmbeddedStatusImpl* getStatusImpl(monger_embedded_v1_status* status) {
    return status ? &status->statusImpl : nullptr;
}

using MobileException = ExceptionForAPI<monger_embedded_v1_error>;

struct ServiceContextDestructor {
    void operator()(monger::ServiceContext* const serviceContext) const noexcept {
        ::monger::embedded::shutdown(serviceContext);
    }
};

using EmbeddedServiceContextPtr = std::unique_ptr<monger::ServiceContext, ServiceContextDestructor>;
}  // namespace
}  // namespace monger

struct monger_embedded_v1_instance {
    ~monger_embedded_v1_instance() {
        invariant(this->clientCount.load() == 0);
        this->parentLib->databaseCount.subtractAndFetch(1);
    }

    monger_embedded_v1_instance(const monger_embedded_v1_instance&) = delete;
    monger_embedded_v1_instance& operator=(const monger_embedded_v1_instance&) = delete;

    explicit monger_embedded_v1_instance(monger_embedded_v1_lib* const p,
                                        const char* const yaml_config)
        : parentLib(p),
          serviceContext(::monger::embedded::initialize(yaml_config)),
          // creating mock transport layer to be able to create sessions
          transportLayer(std::make_unique<monger::transport::TransportLayerMock>()) {
        if (!this->serviceContext) {
            throw ::monger::MobileException{
                MONGO_EMBEDDED_V1_ERROR_DB_INITIALIZATION_FAILED,
                "The MongerDB Embedded Library Failed to initialize the Service Context"};
        }

        this->parentLib->databaseCount.addAndFetch(1);
    }

    monger_embedded_v1_lib* parentLib;
    monger::AtomicWord<int> clientCount;

    monger::EmbeddedServiceContextPtr serviceContext;
    std::unique_ptr<monger::transport::TransportLayerMock> transportLayer;
};

struct monger_embedded_v1_client {
    ~monger_embedded_v1_client() {
        this->parent_db->clientCount.subtractAndFetch(1);
    }

    explicit monger_embedded_v1_client(monger_embedded_v1_instance* const db)
        : parent_db(db),
          client(db->serviceContext->makeClient("embedded", db->transportLayer->createSession())) {
        this->parent_db->clientCount.addAndFetch(1);
    }

    monger_embedded_v1_client(const monger_embedded_v1_client&) = delete;
    monger_embedded_v1_client& operator=(const monger_embedded_v1_client&) = delete;

    monger_embedded_v1_instance* const parent_db;
    monger::ServiceContext::UniqueClient client;

    std::vector<unsigned char> output;
    monger::DbResponse response;
};

namespace monger {
namespace {

std::unique_ptr<monger_embedded_v1_lib> library;

void registerLogCallback(monger_embedded_v1_lib* const lib,
                         const monger_embedded_v1_log_callback logCallback,
                         void* const logUserData) {
    using logger::globalLogDomain;
    using logger::MessageEventEphemeral;
    using logger::MessageEventUnadornedEncoder;

    lib->logCallbackHandle = globalLogDomain()->attachAppender(
        std::make_unique<embedded::EmbeddedLogAppender<MessageEventEphemeral>>(
            logCallback, logUserData, std::make_unique<MessageEventUnadornedEncoder>()));
}

monger_embedded_v1_lib* capi_lib_init(monger_embedded_v1_init_params const* params) try {
    if (library) {
        throw MobileException{
            MONGO_EMBEDDED_V1_ERROR_LIBRARY_ALREADY_INITIALIZED,
            "Cannot initialize the MongerDB Embedded Library when it is already initialized."};
    }

    auto lib = std::make_unique<monger_embedded_v1_lib>();

    // TODO(adam.martin): Fold all of this log initialization into the ctor of lib.
    if (params) {
        using logger::globalLogManager;
        // The standard console log appender may or may not be installed here, depending if this is
        // the first time we initialize the library or not. Make sure we handle both cases.
        if (params->log_flags & MONGO_EMBEDDED_V1_LOG_STDOUT) {
            if (!globalLogManager()->isDefaultConsoleAppenderAttached())
                globalLogManager()->reattachDefaultConsoleAppender();
        } else {
            if (globalLogManager()->isDefaultConsoleAppenderAttached())
                globalLogManager()->detachDefaultConsoleAppender();
        }

        if ((params->log_flags & MONGO_EMBEDDED_V1_LOG_CALLBACK) && params->log_callback) {
            registerLogCallback(lib.get(), params->log_callback, params->log_user_data);
        }
    }

    library = std::move(lib);

    return library.get();
} catch (...) {
    // Make sure that no actual logger is attached if library cannot be initialized.  Also prevent
    // exception leaking failures here.
    []() noexcept {
        using logger::globalLogManager;
        if (globalLogManager()->isDefaultConsoleAppenderAttached())
            globalLogManager()->detachDefaultConsoleAppender();
    }
    ();
    throw;
}

void capi_lib_fini(monger_embedded_v1_lib* const lib) {
    if (!lib) {
        throw MobileException{
            MONGO_EMBEDDED_V1_ERROR_INVALID_LIB_HANDLE,
            "Cannot close a `NULL` pointer referencing a MongerDB Embedded Library Instance"};
    }

    if (!library) {
        throw MobileException{
            MONGO_EMBEDDED_V1_ERROR_LIBRARY_NOT_INITIALIZED,
            "Cannot close the MongerDB Embedded Library when it is not initialized"};
    }

    if (library.get() != lib) {
        throw MobileException{MONGO_EMBEDDED_V1_ERROR_INVALID_LIB_HANDLE,
                              "Invalid MongerDB Embedded Library handle."};
    }

    // This check is not possible to 100% guarantee.  It is a best effort.  The documentation of
    // this API says that the behavior of closing a `lib` with open handles is undefined, but may
    // provide diagnostic errors in some circumstances.
    if (lib->databaseCount.load() > 0) {
        throw MobileException{
            MONGO_EMBEDDED_V1_ERROR_HAS_DB_HANDLES_OPEN,
            "Cannot close the MongerDB Embedded Library when it has database handles still open."};
    }

    library = nullptr;
}

monger_embedded_v1_instance* instance_new(monger_embedded_v1_lib* const lib,
                                         const char* const yaml_config) {
    if (!library) {
        throw MobileException{MONGO_EMBEDDED_V1_ERROR_LIBRARY_NOT_INITIALIZED,
                              "Cannot create a new database handle when the MongerDB Embedded "
                              "Library is not yet initialized."};
    }

    if (library.get() != lib) {
        throw MobileException{MONGO_EMBEDDED_V1_ERROR_INVALID_LIB_HANDLE,
                              "Cannot create a new database handle when the MongerDB Embedded "
                              "Library is not yet initialized."};
    }

    if (lib->onlyDB) {
        throw MobileException{MONGO_EMBEDDED_V1_ERROR_DB_MAX_OPEN,
                              "The maximum number of permitted database handles for the MongerDB "
                              "Embedded Library have been opened."};
    }

    lib->onlyDB = std::make_unique<monger_embedded_v1_instance>(lib, yaml_config);

    return lib->onlyDB.get();
}

void instance_destroy(monger_embedded_v1_instance* const db) {
    if (!library) {
        throw MobileException{MONGO_EMBEDDED_V1_ERROR_LIBRARY_NOT_INITIALIZED,
                              "Cannot destroy a database handle when the MongerDB Embedded Library "
                              "is not yet initialized."};
    }

    if (!db) {
        throw MobileException{
            MONGO_EMBEDDED_V1_ERROR_INVALID_DB_HANDLE,
            "Cannot close a `NULL` pointer referencing a MongerDB Embedded Database"};
    }

    if (db != library->onlyDB.get()) {
        throw MobileException{
            MONGO_EMBEDDED_V1_ERROR_INVALID_DB_HANDLE,
            "Cannot close the specified MongerDB Embedded Database, as it is not a valid instance."};
    }

    if (db->clientCount.load() > 0) {
        throw MobileException{
            MONGO_EMBEDDED_V1_ERROR_DB_CLIENTS_OPEN,
            "Cannot close a MongerDB Embedded Database instance while it has open clients"};
    }

    library->onlyDB = nullptr;
}

monger_embedded_v1_client* client_new(monger_embedded_v1_instance* const db) {
    if (!library) {
        throw MobileException{MONGO_EMBEDDED_V1_ERROR_LIBRARY_NOT_INITIALIZED,
                              "Cannot create a new client handle when the MongerDB Embedded Library "
                              "is not yet initialized."};
    }

    if (!db) {
        throw MobileException{MONGO_EMBEDDED_V1_ERROR_INVALID_DB_HANDLE,
                              "Cannot use a `NULL` pointer referencing a MongerDB Embedded Database "
                              "when creating a new client"};
    }

    if (db != library->onlyDB.get()) {
        throw MobileException{MONGO_EMBEDDED_V1_ERROR_INVALID_DB_HANDLE,
                              "The specified MongerDB Embedded Database instance cannot be used to "
                              "create a new client because it is invalid."};
    }

    return new monger_embedded_v1_client(db);
}

void client_destroy(monger_embedded_v1_client* const client) {
    if (!library) {
        throw MobileException(MONGO_EMBEDDED_V1_ERROR_LIBRARY_NOT_INITIALIZED,
                              "Cannot destroy a database handle when the MongerDB Embedded Library "
                              "is not yet initialized.");
    }

    if (!client) {
        throw MobileException{
            MONGO_EMBEDDED_V1_ERROR_INVALID_CLIENT_HANDLE,
            "Cannot destroy a `NULL` pointer referencing a MongerDB Embedded Database Client"};
    }

    delete client;
}

class ClientGuard {
    ClientGuard(const ClientGuard&) = delete;
    void operator=(const ClientGuard&) = delete;

public:
    explicit ClientGuard(monger_embedded_v1_client* const client) : _client(client) {
        monger::Client::setCurrent(std::move(client->client));
    }

    ~ClientGuard() {
        _client->client = monger::Client::releaseCurrent();
    }

private:
    monger_embedded_v1_client* const _client;
};

void client_wire_protocol_rpc(monger_embedded_v1_client* const client,
                              const void* input,
                              const size_t input_size,
                              void** const output,
                              size_t* const output_size) {
    ClientGuard clientGuard(client);

    auto opCtx = cc().makeOperationContext();
    auto sep = client->parent_db->serviceContext->getServiceEntryPoint();

    auto sb = SharedBuffer::allocate(input_size);
    memcpy(sb.get(), input, input_size);

    Message msg(std::move(sb));

    client->response = sep->handleRequest(opCtx.get(), msg);

    // Note that we skip OP_MSG's optional checksum for embedded.
    MsgData::View outMessage(client->response.response.buf());
    outMessage.setId(nextMessageId());
    outMessage.setResponseToMsgId(msg.header().getId());

    // The results of the computations used to fill out-parameters need to be captured and processed
    // before setting the output parameters themselves, in order to maintain the strong-guarantee
    // part of the contract of this function.
    auto outParams =
        std::make_tuple(client->response.response.size(), client->response.response.buf());

    // We force the output parameters to be set in a `noexcept` enabled way.  If the operation
    // itself
    // is safely noexcept, we just run it, otherwise we force a `noexcept` over it to catch errors.
    if (noexcept(std::tie(*output_size, *output) = std::move(outParams))) {
        std::tie(*output_size, *output) = std::move(outParams);
    } else {
        // Assigning primitives in a tied tuple should be noexcept, so we force it to be so, for
        // our purposes.  This facilitates a runtime check should something WEIRD happen.
        [ output, output_size, &outParams ]() noexcept {
            std::tie(*output_size, *output) = std::move(outParams);
        }
        ();
    }
}

int capi_status_get_error(const monger_embedded_v1_status* const status) noexcept {
    invariant(status);
    return status->statusImpl.error;
}

const char* capi_status_get_what(const monger_embedded_v1_status* const status) noexcept {
    invariant(status);
    return status->statusImpl.what.c_str();
}

int capi_status_get_code(const monger_embedded_v1_status* const status) noexcept {
    invariant(status);
    return status->statusImpl.exception_code;
}
}  // namespace
}  // namespace monger

extern "C" {
monger_embedded_v1_lib* MONGO_API_CALL monger_embedded_v1_lib_init(
    const monger_embedded_v1_init_params* const params, monger_embedded_v1_status* const statusPtr) {
    return enterCXX(monger::getStatusImpl(statusPtr),
                    [&]() { return monger::capi_lib_init(params); });
}

int MONGO_API_CALL monger_embedded_v1_lib_fini(monger_embedded_v1_lib* const lib,
                                              monger_embedded_v1_status* const statusPtr) {
    return enterCXX(monger::getStatusImpl(statusPtr), [&]() { return monger::capi_lib_fini(lib); });
}

monger_embedded_v1_instance* MONGO_API_CALL
monger_embedded_v1_instance_create(monger_embedded_v1_lib* lib,
                                  const char* const yaml_config,
                                  monger_embedded_v1_status* const statusPtr) {
    return enterCXX(monger::getStatusImpl(statusPtr),
                    [&]() { return monger::instance_new(lib, yaml_config); });
}

int MONGO_API_CALL monger_embedded_v1_instance_destroy(monger_embedded_v1_instance* const db,
                                                      monger_embedded_v1_status* const statusPtr) {
    return enterCXX(monger::getStatusImpl(statusPtr), [&]() { return monger::instance_destroy(db); });
}

monger_embedded_v1_client* MONGO_API_CALL monger_embedded_v1_client_create(
    monger_embedded_v1_instance* const db, monger_embedded_v1_status* const statusPtr) {
    return enterCXX(monger::getStatusImpl(statusPtr), [&]() { return monger::client_new(db); });
}

int MONGO_API_CALL monger_embedded_v1_client_destroy(monger_embedded_v1_client* const client,
                                                    monger_embedded_v1_status* const statusPtr) {
    return enterCXX(monger::getStatusImpl(statusPtr),
                    [&]() { return monger::client_destroy(client); });
}

int MONGO_API_CALL monger_embedded_v1_client_invoke(monger_embedded_v1_client* const client,
                                                   const void* input,
                                                   const size_t input_size,
                                                   void** const output,
                                                   size_t* const output_size,
                                                   monger_embedded_v1_status* const statusPtr) {
    return enterCXX(monger::getStatusImpl(statusPtr), [&]() {
        return monger::client_wire_protocol_rpc(client, input, input_size, output, output_size);
    });
}

int MONGO_API_CALL
monger_embedded_v1_status_get_error(const monger_embedded_v1_status* const status) {
    return monger::capi_status_get_error(status);
}

const char* MONGO_API_CALL
monger_embedded_v1_status_get_explanation(const monger_embedded_v1_status* const status) {
    return monger::capi_status_get_what(status);
}

int MONGO_API_CALL monger_embedded_v1_status_get_code(const monger_embedded_v1_status* const status) {
    return monger::capi_status_get_code(status);
}

monger_embedded_v1_status* MONGO_API_CALL monger_embedded_v1_status_create(void) {
    return new monger_embedded_v1_status;
}

void MONGO_API_CALL monger_embedded_v1_status_destroy(monger_embedded_v1_status* const status) {
    delete status;
}

}  // extern "C"
