// Copyright (C) MongerDB, Inc. 2014-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

package db

import (
	"context"
	"fmt"

	"go.mongerdb.org/monger-driver/bson"
	"go.mongerdb.org/monger-driver/monger"
	"go.mongerdb.org/monger-driver/monger/options"
)

// BufferedBulkInserter implements a bufio.Writer-like design for queuing up
// documents and inserting them in bulk when the given doc limit (or max
// message size) is reached. Must be flushed at the end to ensure that all
// documents are written.
type BufferedBulkInserter struct {
	collection    *monger.Collection
	writeModels   []monger.WriteModel
	docLimit      int
	docCount      int
	bulkWriteOpts *options.BulkWriteOptions
	upsert        bool
}

func newBufferedBulkInserter(collection *monger.Collection, docLimit int, ordered bool) *BufferedBulkInserter {
	bb := &BufferedBulkInserter{
		collection:    collection,
		bulkWriteOpts: options.BulkWrite().SetOrdered(ordered),
		docLimit:      docLimit,
		writeModels:   make([]monger.WriteModel, 0, docLimit),
	}
	return bb
}

// NewOrderedBufferedBulkInserter returns an initialized BufferedBulkInserter for performing ordered bulk writes.
func NewOrderedBufferedBulkInserter(collection *monger.Collection, docLimit int) *BufferedBulkInserter {
	return newBufferedBulkInserter(collection, docLimit, true)
}

// NewOrderedBufferedBulkInserter returns an initialized BufferedBulkInserter for performing unordered bulk writes.
func NewUnorderedBufferedBulkInserter(collection *monger.Collection, docLimit int) *BufferedBulkInserter {
	return newBufferedBulkInserter(collection, docLimit, false)
}

func (bb *BufferedBulkInserter) SetOrdered(ordered bool) *BufferedBulkInserter {
	bb.bulkWriteOpts.SetOrdered(ordered)
	return bb
}

func (bb *BufferedBulkInserter) SetBypassDocumentValidation(bypass bool) *BufferedBulkInserter {
	bb.bulkWriteOpts.SetBypassDocumentValidation(bypass)
	return bb
}

func (bb *BufferedBulkInserter) SetUpsert(upsert bool) *BufferedBulkInserter {
	bb.upsert = upsert
	return bb
}

// throw away the old bulk and init a new one
func (bb *BufferedBulkInserter) resetBulk() {
	bb.writeModels = bb.writeModels[:0]
	bb.docCount = 0
}

// Insert adds a document to the buffer for bulk insertion. If the buffer becomes full, the bulk write is performed, returning
// any error that occurs.
func (bb *BufferedBulkInserter) Insert(doc interface{}) (*monger.BulkWriteResult, error) {
	rawBytes, err := bson.Marshal(doc)
	if err != nil {
		return nil, fmt.Errorf("bson encoding error: %v", err)
	}

	return bb.InsertRaw(rawBytes)
}

// Update adds a document to the buffer for bulk update. If the buffer becomes full, the bulk write is performed, returning
// any error that occurs.
func (bb *BufferedBulkInserter) Update(selector, update bson.D) (*monger.BulkWriteResult, error) {
	return bb.addModel(monger.NewUpdateOneModel().SetFilter(selector).SetUpdate(update).SetUpsert(bb.upsert))
}

// Replace adds a document to the buffer for bulk replacement. If the buffer becomes full, the bulk write is performed, returning
// any error that occurs.
func (bb *BufferedBulkInserter) Replace(selector, replacement bson.D) (*monger.BulkWriteResult, error) {
	return bb.addModel(monger.NewReplaceOneModel().SetFilter(selector).SetReplacement(replacement).SetUpsert(bb.upsert))
}

// InsertRaw adds a document, represented as raw bson bytes, to the buffer for bulk insertion. If the buffer becomes full,
// the bulk write is performed, returning any error that occurs.
func (bb *BufferedBulkInserter) InsertRaw(rawBytes []byte) (*monger.BulkWriteResult, error) {
	return bb.addModel(monger.NewInsertOneModel().SetDocument(rawBytes))
}

// addModel adds a WriteModel to the buffer. If the buffer becomes full, the bulk write is performed, returning any error
// that occurs.
func (bb *BufferedBulkInserter) addModel(model monger.WriteModel) (*monger.BulkWriteResult, error) {
	bb.docCount++
	bb.writeModels = append(bb.writeModels, model)

	if bb.docCount >= bb.docLimit {
		return bb.Flush()
	}

	return nil, nil
}

// Flush writes all buffered documents in one bulk write and then resets the buffer.
func (bb *BufferedBulkInserter) Flush() (*monger.BulkWriteResult, error) {
	if bb.docCount == 0 {
		return nil, nil
	}

	defer bb.resetBulk()
	return bb.collection.BulkWrite(context.Background(), bb.writeModels, bb.bulkWriteOpts)
}
