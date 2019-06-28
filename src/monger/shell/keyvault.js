//  Class that allows the monger shell to talk to the mongerdb KeyVault.
//  Loaded only into the enterprise module.

Monger.prototype.getKeyVault = function() {
    return new KeyVault(this);
};

class KeyVault {
    constructor(monger) {
        this.monger = monger;
        var collection = monger.getDataKeyCollection();
        this.keyColl = collection;
        this.keyColl.createIndex(
            {keyAltNames: 1},
            {unique: true, partialFilterExpression: {keyAltNames: {$exists: true}}});
    }

    createKey(kmsProvider, customerMasterKey, keyAltNames = undefined) {
        if (typeof kmsProvider !== "string") {
            return "TypeError: kmsProvider must be of String type.";
        }

        if (typeof customerMasterKey !== "string") {
            return "TypeError: customer master key must be of String type.";
        }

        var masterKeyAndMaterial = this.monger.generateDataKey(kmsProvider, customerMasterKey);
        var masterKey = masterKeyAndMaterial.masterKey;

        var current = ISODate();

        var doc = {
            "_id": UUID(),
            "keyMaterial": masterKeyAndMaterial.keyMaterial,
            "creationDate": current,
            "updateDate": current,
            "status": NumberInt(0),
            "version": NumberLong(0),
            "masterKey": masterKey,
        };

        if (keyAltNames) {
            if (!Array.isArray(keyAltNames)) {
                return "TypeError: key alternate names must be of Array type.";
            }

            let i = 0;
            for (i = 0; i < keyAltNames.length; i++) {
                if (typeof keyAltNames[i] !== "string") {
                    return "TypeError: items in key alternate names must be of String type.";
                }
            }

            doc.keyAltNames = keyAltNames;
        }

        return this.keyColl.insert(doc);
    }

    getKey(keyId) {
        return this.keyColl.find({"_id": keyId});
    }

    getKeyByAltName(keyAltName) {
        return this.keyColl.find({"keyAltNames": keyAltName});
    }

    deleteKey(keyId) {
        return this.keyColl.deleteOne({"_id": keyId});
    }

    getKeys() {
        return this.keyColl.find();
    }

    addKeyAlternateName(keyId, keyAltName) {
        // keyAltName is not allowed to be an array or an object. In javascript,
        // typeof array is object.
        if (typeof keyAltName === "object") {
            return "TypeError: key alternate name cannot be object or array type.";
        }
        return this.keyColl.findAndModify({
            query: {"_id": keyId},
            update: {$push: {"keyAltNames": keyAltName}, $currentDate: {"updateDate": true}},
        });
    }

    removeKeyAlternateName(keyId, keyAltName) {
        if (typeof keyAltName === "object") {
            return "TypeError: key alternate name cannot be object or array type.";
        }
        const ret = this.keyColl.findAndModify({
            query: {"_id": keyId},
            update: {$pull: {"keyAltNames": keyAltName}, $currentDate: {"updateDate": true}}
        });

        if (ret != null && ret.keyAltNames.length === 1 && ret.keyAltNames[0] === keyAltName) {
            // Remove the empty array to prevent duplicate key violations
            return this.keyColl.findAndModify({
                query: {"_id": keyId, "keyAltNames": undefined},
                update: {$unset: {"keyAltNames": ""}, $currentDate: {"updateDate": true}}
            });
        }
        return ret;
    }
}

class ClientEncryption {
    constructor(monger) {
        this.monger = monger;
    }

    encrypt() {
        return this.monger.encrypt.apply(this.monger, arguments);
    }

    decrypt() {
        return this.monger.decrypt.apply(this.monger, arguments);
    }
}

Monger.prototype.getClientEncryption = function() {
    return new ClientEncryption(this);
};
