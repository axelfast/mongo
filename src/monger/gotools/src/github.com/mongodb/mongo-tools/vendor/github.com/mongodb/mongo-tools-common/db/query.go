package db

import (
	"go.mongerdb.org/monger-driver/bson"
	"go.mongerdb.org/monger-driver/monger"
	mopt "go.mongerdb.org/monger-driver/monger/options"
)

// DeferredQuery represents a deferred query
type DeferredQuery struct {
	Coll      *monger.Collection
	Filter    interface{}
	Hint      interface{}
	LogReplay bool
}

// Count issues a count command. We don't use the Hint because
// that's not supported with older servers.
func (q *DeferredQuery) Count() (int, error) {
	opt := mopt.Count()
	filter := q.Filter
	if filter == nil {
		filter = bson.D{}
	}
	c, err := q.Coll.CountDocuments(nil, filter, opt)
	return int(c), err
}

func (q *DeferredQuery) Iter() (*monger.Cursor, error) {
	opts := mopt.Find()
	if q.Hint != nil {
		opts.SetHint(q.Hint)
	}
	if q.LogReplay {
		opts.SetOplogReplay(true)
	}
	filter := q.Filter
	if filter == nil {
		filter = bson.D{}
	}
	return q.Coll.Find(nil, filter, opts)
}
