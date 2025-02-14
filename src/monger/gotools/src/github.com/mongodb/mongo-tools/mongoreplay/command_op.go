// Copyright (C) MongoDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package mongerreplay

import (
	"encoding/json"
	"fmt"
	"io"
	"time"

	mgo "github.com/10gen/llmgo"
	"github.com/10gen/llmgo/bson"
)

// CommandOp is a struct for parsing OP_COMMAND as defined here:
// https://github.com/mongerdb/monger/blob/master/src/monger/rpc/command_request.h.
type CommandOp struct {
	Header MsgHeader
	mgo.CommandOp
}

// CommandGetMore is a struct representing a special case of an OP_COMMAND which
// has commandName 'getmore'.  It implements the cursorsRewriteable interface
// and has fields for caching the found cursors so that multiple calls to these
// methods do not incur the overhead of searching the underlying bson for the
// cursorID.
type CommandGetMore struct {
	CommandOp
	cachedCursor *int64
}

// getCursorIDs is an implementation of the cursorsRewriteable interface method.
// It returns an array of the cursors contained in the CommandGetMore, which is
// only ever one cursor. It may return an error if unmarshalling the command's
// bson fails.
func (gmCommand *CommandGetMore) getCursorIDs() ([]int64, error) {
	if gmCommand.cachedCursor != nil {
		return []int64{*gmCommand.cachedCursor}, nil
	}
	cursorID, err := getGetMoreCursorID(gmCommand.CommandArgs)
	if err != nil {
		return []int64{}, err
	}
	gmCommand.cachedCursor = &cursorID
	return []int64{*gmCommand.cachedCursor}, err
}

// setCursorIDs is an implementation of the cursorsRewriteable interface method.
// It takes an array of cursors that will function as the new cursors for
// this operation.  If there is more than one cursorID in the array, it
// errors, as it only ever expects one. It may also error if unmarshalling the
// underlying bson fails.
func (gmCommand *CommandGetMore) setCursorIDs(newCursorIDs []int64) error {
	newDoc, newCursorID, err := setCursorID(gmCommand.CommandArgs, newCursorIDs)
	if err != nil {
		return err
	}
	gmCommand.CommandArgs = &newDoc
	gmCommand.cachedCursor = &newCursorID
	return nil
}

func (op *CommandOp) String() string {
	commandArgsString, metadataString, inputDocsString, err := op.getOpBodyString()
	if err != nil {
		return fmt.Sprintf("%v", err)
	}
	return fmt.Sprintf("OpCommand %v %v %v %v %v", op.Database, op.CommandName, commandArgsString, metadataString, inputDocsString)
}

// Meta returns metadata about the operation, useful for analysis of traffic.
func (op *CommandOp) Meta() OpMetadata {
	return OpMetadata{"op_command",
		op.Database,
		op.CommandName,
		map[string]interface{}{
			"metadata":     op.Metadata,
			"command_args": op.CommandArgs,
			"input_docs":   op.InputDocs,
		},
	}
}

// Abbreviated returns a serialization of the OpCommand, abbreviated so it
// doesn't exceed the given number of characters.
func (op *CommandOp) Abbreviated(chars int) string {
	commandArgsString, metadataString, inputDocsString, err := op.getOpBodyString()
	if err != nil {
		return fmt.Sprintf("%v", err)
	}
	return fmt.Sprintf("OpCommand db:%v args:%v metadata:%v inputdocs:%v",
		op.Database, Abbreviate(commandArgsString, chars),
		Abbreviate(metadataString, chars), Abbreviate(inputDocsString, chars))
}

// OpCode returns the OpCode for a CommandOp.
func (op *CommandOp) OpCode() OpCode {
	return OpCodeCommand
}

func (op *CommandOp) getOpBodyString() (string, string, string, error) {
	commandArgsDoc, err := ConvertBSONValueToJSON(op.CommandArgs)
	if err != nil {
		return "", "", "", fmt.Errorf("ConvertBSONValueToJSON err: %#v - %v", op, err)
	}

	commandArgsAsJSON, err := json.Marshal(commandArgsDoc)
	if err != nil {
		return "", "", "", fmt.Errorf("json marshal err: %#v - %v", op, err)
	}

	metadataDocs, err := ConvertBSONValueToJSON(op.Metadata)
	if err != nil {
		return "", "", "", fmt.Errorf("ConvertBSONValueToJSON err: %#v - %v", op, err)
	}

	metadataAsJSON, err := json.Marshal(metadataDocs)
	if err != nil {
		return "", "", "", fmt.Errorf("json marshal err: %#v - %v", op, err)
	}

	var inputDocsString string

	if len(op.InputDocs) != 0 {
		inputDocs, err := ConvertBSONValueToJSON(op.InputDocs)
		if err != nil {
			return "", "", "", fmt.Errorf("ConvertBSONValueToJSON err: %#v - %v", op, err)
		}

		inputDocsAsJSON, err := json.Marshal(inputDocs)
		if err != nil {
			return "", "", "", fmt.Errorf("json marshal err: %#v - %v", op, err)
		}
		inputDocsString = string(inputDocsAsJSON)
	}
	return string(commandArgsAsJSON), string(metadataAsJSON), inputDocsString, nil
}

// FromReader extracts data from a serialized OpCommand into its concrete
// structure.
func (op *CommandOp) FromReader(r io.Reader) error {
	database, err := readCStringFromReader(r)
	if err != nil {
		return err
	}
	op.Database = string(database)

	commandName, err := readCStringFromReader(r)
	if err != nil {
		return err
	}
	op.CommandName = string(commandName)

	commandArgsAsSlice, err := ReadDocument(r)
	if err != nil {
		return err
	}
	op.CommandArgs = &bson.Raw{}
	err = bson.Unmarshal(commandArgsAsSlice, op.CommandArgs)
	if err != nil {
		return err
	}

	metadataAsSlice, err := ReadDocument(r)
	if err != nil {
		return err
	}
	op.Metadata = &bson.Raw{}
	err = bson.Unmarshal(metadataAsSlice, op.Metadata)
	if err != nil {
		return err
	}

	lengthRead := len(database) + 1 + len(commandName) + 1 + len(commandArgsAsSlice) + len(metadataAsSlice)

	op.InputDocs = make([]interface{}, 0)
	docLen := 0
	for lengthRead+docLen < int(op.Header.MessageLength)-MsgHeaderLen {
		docAsSlice, err := ReadDocument(r)
		doc := &bson.Raw{}
		err = bson.Unmarshal(docAsSlice, doc)
		if err != nil {
			return err
		}
		docLen += len(docAsSlice)
		op.InputDocs = append(op.InputDocs, doc)
	}
	return nil
}

func (op *CommandOp) Preprocess() {
	op.CommandOp.Metadata = nil
}

// Execute performs the CommandOp on a given session, yielding the reply when
// successful (and an error otherwise).
func (op *CommandOp) Execute(socket *mgo.MongerSocket) (Replyable, error) {
	before := time.Now()
	metadata, commandReply, replyData, resultReply, err := mgo.ExecOpWithReply(socket, &op.CommandOp)
	after := time.Now()
	if err != nil {
		return nil, err
	}
	mgoCommandReplyOp, ok := resultReply.(*mgo.CommandReplyOp)
	if !ok {
		panic("reply from execution was not the correct type")
	}
	commandReplyOp := &CommandReplyOp{
		CommandReplyOp: *mgoCommandReplyOp,
	}

	commandReplyOp.Metadata = &bson.Raw{}
	err = bson.Unmarshal(metadata, commandReplyOp.Metadata)
	if err != nil {
		return nil, err
	}
	commandReplyAsRaw := &bson.Raw{}
	err = bson.Unmarshal(commandReply, commandReplyAsRaw)
	if err != nil {
		return nil, err
	}
	commandReplyOp.CommandReply = commandReplyAsRaw

	cursorDocs, err := getCursorDocs(commandReplyAsRaw)
	if err != nil {
		return nil, err
	}
	commandReplyOp.Docs = cursorDocs

	for _, d := range replyData {
		dataDoc := &bson.Raw{}
		err = bson.Unmarshal(d, &dataDoc)
		if err != nil {
			return nil, err
		}
		commandReplyOp.OutputDocs = append(commandReplyOp.OutputDocs, dataDoc)
	}
	commandReplyOp.Latency = after.Sub(before)
	return commandReplyOp, nil

}
