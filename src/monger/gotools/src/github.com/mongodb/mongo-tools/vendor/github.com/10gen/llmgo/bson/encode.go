// Copyright (C) MongoDB, Inc. 2015-present.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may
// not use this file except in compliance with the License. You may obtain
// a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
//
// Based on gopkg.io/mgo.v2 by Gustavo Niemeyer.
// See THIRD-PARTY-NOTICES for original license terms.

package bson

import (
	"encoding/json"
	"fmt"
	"math"
	"net/url"
	"reflect"
	"strconv"
	"time"
)

// --------------------------------------------------------------------------
// Some internal infrastructure.

var (
	typeBinary         = reflect.TypeOf(Binary{})
	typeObjectId       = reflect.TypeOf(ObjectId(""))
	typeDBPointer      = reflect.TypeOf(DBPointer{"", ObjectId("")})
	typeSymbol         = reflect.TypeOf(Symbol(""))
	typeMongerTimestamp = reflect.TypeOf(MongerTimestamp(0))
	typeOrderKey       = reflect.TypeOf(MinKey)
	typeDocElem        = reflect.TypeOf(DocElem{})
	typeRawDocElem     = reflect.TypeOf(RawDocElem{})
	typeRaw            = reflect.TypeOf(Raw{})
	typeURL            = reflect.TypeOf(url.URL{})
	typeTime           = reflect.TypeOf(time.Time{})
	typeString         = reflect.TypeOf("")
	typeJSONNumber     = reflect.TypeOf(json.Number(""))
)

const itoaCacheSize = 32

var itoaCache []string

func init() {
	itoaCache = make([]string, itoaCacheSize)
	for i := 0; i != itoaCacheSize; i++ {
		itoaCache[i] = strconv.Itoa(i)
	}
}

func itoa(i int) string {
	if i < itoaCacheSize {
		return itoaCache[i]
	}
	return strconv.Itoa(i)
}

// --------------------------------------------------------------------------
// Marshaling of the document value itself.

type encoder struct {
	out []byte
}

func (e *encoder) addDoc(v reflect.Value) {
	for {
		if vi, ok := v.Interface().(Getter); ok {
			getv, err := vi.GetBSON()
			if err != nil {
				panic(err)
			}
			v = reflect.ValueOf(getv)
			continue
		}
		if v.Kind() == reflect.Ptr {
			v = v.Elem()
			continue
		}
		break
	}

	if v.Type() == typeRaw {
		raw := v.Interface().(Raw)
		if raw.Kind != 0x03 && raw.Kind != 0x00 {
			panic("Attempted to marshal Raw kind " + strconv.Itoa(int(raw.Kind)) + " as a document")
		}
		if len(raw.Data) == 0 {
			panic("Attempted to marshal empty Raw document")
		}
		e.addBytes(raw.Data...)
		return
	}

	start := e.reserveInt32()

	switch v.Kind() {
	case reflect.Map:
		e.addMap(v)
	case reflect.Struct:
		e.addStruct(v)
	case reflect.Array, reflect.Slice:
		e.addSlice(v)
	default:
		panic("Can't marshal " + v.Type().String() + " as a BSON document")
	}

	e.addBytes(0)
	e.setInt32(start, int32(len(e.out)-start))
}

func (e *encoder) addMap(v reflect.Value) {
	for _, k := range v.MapKeys() {
		e.addElem(k.String(), v.MapIndex(k), false)
	}
}

func (e *encoder) addStruct(v reflect.Value) {
	sinfo, err := getStructInfo(v.Type())
	if err != nil {
		panic(err)
	}
	var value reflect.Value
	if sinfo.InlineMap >= 0 {
		m := v.Field(sinfo.InlineMap)
		if m.Len() > 0 {
			for _, k := range m.MapKeys() {
				ks := k.String()
				if _, found := sinfo.FieldsMap[ks]; found {
					panic(fmt.Sprintf("Can't have key %q in inlined map; conflicts with struct field", ks))
				}
				e.addElem(ks, m.MapIndex(k), false)
			}
		}
	}
	for _, info := range sinfo.FieldsList {
		if info.Inline == nil {
			value = v.Field(info.Num)
		} else {
			value = v.FieldByIndex(info.Inline)
		}
		if info.OmitEmpty && isZero(value) {
			continue
		}
		e.addElem(info.Key, value, info.MinSize)
	}
}

func isZero(v reflect.Value) bool {
	switch v.Kind() {
	case reflect.String:
		return len(v.String()) == 0
	case reflect.Ptr, reflect.Interface:
		return v.IsNil()
	case reflect.Slice:
		return v.Len() == 0
	case reflect.Map:
		return v.Len() == 0
	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		return v.Int() == 0
	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr:
		return v.Uint() == 0
	case reflect.Float32, reflect.Float64:
		return v.Float() == 0
	case reflect.Bool:
		return !v.Bool()
	case reflect.Struct:
		vt := v.Type()
		if vt == typeTime {
			return v.Interface().(time.Time).IsZero()
		}
		for i := 0; i < v.NumField(); i++ {
			if vt.Field(i).PkgPath != "" {
				continue // Private field
			}
			if !isZero(v.Field(i)) {
				return false
			}
		}
		return true
	}
	return false
}

func (e *encoder) addSlice(v reflect.Value) {
	vi := v.Interface()
	if d, ok := vi.(D); ok {
		for _, elem := range d {
			e.addElem(elem.Name, reflect.ValueOf(elem.Value), false)
		}
		return
	}
	if d, ok := vi.(RawD); ok {
		for _, elem := range d {
			e.addElem(elem.Name, reflect.ValueOf(elem.Value), false)
		}
		return
	}
	l := v.Len()
	et := v.Type().Elem()
	if et == typeDocElem {
		for i := 0; i < l; i++ {
			elem := v.Index(i).Interface().(DocElem)
			e.addElem(elem.Name, reflect.ValueOf(elem.Value), false)
		}
		return
	}
	if et == typeRawDocElem {
		for i := 0; i < l; i++ {
			elem := v.Index(i).Interface().(RawDocElem)
			e.addElem(elem.Name, reflect.ValueOf(elem.Value), false)
		}
		return
	}
	for i := 0; i < l; i++ {
		e.addElem(itoa(i), v.Index(i), false)
	}
}

// --------------------------------------------------------------------------
// Marshaling of elements in a document.

func (e *encoder) addElemName(kind byte, name string) {
	e.addBytes(kind)
	e.addBytes([]byte(name)...)
	e.addBytes(0)
}

func (e *encoder) addElem(name string, v reflect.Value, minSize bool) {

	if !v.IsValid() {
		e.addElemName('\x0A', name)
		return
	}

	if getter, ok := v.Interface().(Getter); ok {
		getv, err := getter.GetBSON()
		if err != nil {
			panic(err)
		}
		e.addElem(name, reflect.ValueOf(getv), minSize)
		return
	}

	switch v.Kind() {

	case reflect.Interface:
		e.addElem(name, v.Elem(), minSize)

	case reflect.Ptr:
		e.addElem(name, v.Elem(), minSize)

	case reflect.String:
		s := v.String()
		switch v.Type() {
		case typeObjectId:
			if len(s) != 12 {
				panic("ObjectIDs must be exactly 12 bytes long (got " +
					strconv.Itoa(len(s)) + ")")
			}
			e.addElemName('\x07', name)
			e.addBytes([]byte(s)...)
		case typeSymbol:
			e.addElemName('\x0E', name)
			e.addStr(s)
		case typeJSONNumber:
			n := v.Interface().(json.Number)
			if i, err := n.Int64(); err == nil {
				e.addElemName('\x12', name)
				e.addInt64(i)
			} else if f, err := n.Float64(); err == nil {
				e.addElemName('\x01', name)
				e.addFloat64(f)
			} else {
				panic("failed to convert json.Number to a number: " + s)
			}
		default:
			e.addElemName('\x02', name)
			e.addStr(s)
		}

	case reflect.Float32, reflect.Float64:
		e.addElemName('\x01', name)
		e.addFloat64(v.Float())

	case reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64, reflect.Uintptr:
		u := v.Uint()
		if int64(u) < 0 {
			panic("BSON has no uint64 type, and value is too large to fit correctly in an int64")
		} else if u <= math.MaxInt32 && (minSize || v.Kind() <= reflect.Uint32) {
			e.addElemName('\x10', name)
			e.addInt32(int32(u))
		} else {
			e.addElemName('\x12', name)
			e.addInt64(int64(u))
		}

	case reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64:
		switch v.Type() {
		case typeMongerTimestamp:
			e.addElemName('\x11', name)
			e.addInt64(v.Int())

		case typeOrderKey:
			if v.Int() == int64(MaxKey) {
				e.addElemName('\x7F', name)
			} else {
				e.addElemName('\xFF', name)
			}

		default:
			i := v.Int()
			if (minSize || v.Type().Kind() != reflect.Int64) && i >= math.MinInt32 && i <= math.MaxInt32 {
				// It fits into an int32, encode as such.
				e.addElemName('\x10', name)
				e.addInt32(int32(i))
			} else {
				e.addElemName('\x12', name)
				e.addInt64(i)
			}
		}

	case reflect.Bool:
		e.addElemName('\x08', name)
		if v.Bool() {
			e.addBytes(1)
		} else {
			e.addBytes(0)
		}

	case reflect.Map:
		e.addElemName('\x03', name)
		e.addDoc(v)

	case reflect.Slice:
		vt := v.Type()
		et := vt.Elem()
		if et.Kind() == reflect.Uint8 {
			e.addElemName('\x05', name)
			e.addBinary('\x00', v.Bytes())
		} else if et == typeDocElem || et == typeRawDocElem {
			e.addElemName('\x03', name)
			e.addDoc(v)
		} else {
			e.addElemName('\x04', name)
			e.addDoc(v)
		}

	case reflect.Array:
		et := v.Type().Elem()
		if et.Kind() == reflect.Uint8 {
			e.addElemName('\x05', name)
			if v.CanAddr() {
				e.addBinary('\x00', v.Slice(0, v.Len()).Interface().([]byte))
			} else {
				n := v.Len()
				e.addInt32(int32(n))
				e.addBytes('\x00')
				for i := 0; i < n; i++ {
					el := v.Index(i)
					e.addBytes(byte(el.Uint()))
				}
			}
		} else {
			e.addElemName('\x04', name)
			e.addDoc(v)
		}

	case reflect.Struct:
		switch s := v.Interface().(type) {

		case Raw:
			kind := s.Kind
			if kind == 0x00 {
				kind = 0x03
			}
			if len(s.Data) == 0 && kind != 0x06 && kind != 0x0A && kind != 0xFF && kind != 0x7F {
				panic("Attempted to marshal empty Raw document")
			}
			e.addElemName(kind, name)
			e.addBytes(s.Data...)

		case Binary:
			e.addElemName('\x05', name)
			e.addBinary(s.Kind, s.Data)

		case DBPointer:
			e.addElemName('\x0C', name)
			e.addStr(s.Namespace)
			if len(s.Id) != 12 {
				panic("ObjectIDs must be exactly 12 bytes long (got " +
					strconv.Itoa(len(s.Id)) + ")")
			}
			e.addBytes([]byte(s.Id)...)

		case RegEx:
			e.addElemName('\x0B', name)
			e.addCStr(s.Pattern)
			e.addCStr(s.Options)

		case JavaScript:
			if s.Scope == nil {
				e.addElemName('\x0D', name)
				e.addStr(s.Code)
			} else {
				e.addElemName('\x0F', name)
				start := e.reserveInt32()
				e.addStr(s.Code)
				e.addDoc(reflect.ValueOf(s.Scope))
				e.setInt32(start, int32(len(e.out)-start))
			}

		case time.Time:
			// MongerDB handles timestamps as milliseconds.
			e.addElemName('\x09', name)
			e.addInt64(s.Unix()*1000 + int64(s.Nanosecond()/1e6))

		case url.URL:
			e.addElemName('\x02', name)
			e.addStr(s.String())

		case undefined:
			e.addElemName('\x06', name)

		default:
			e.addElemName('\x03', name)
			e.addDoc(v)
		}

	default:
		panic("Can't marshal " + v.Type().String() + " in a BSON document")
	}
}

// --------------------------------------------------------------------------
// Marshaling of base types.

func (e *encoder) addBinary(subtype byte, v []byte) {
	if subtype == 0x02 {
		// Wonder how that brilliant idea came to life. Obsolete, luckily.
		e.addInt32(int32(len(v) + 4))
		e.addBytes(subtype)
		e.addInt32(int32(len(v)))
	} else {
		e.addInt32(int32(len(v)))
		e.addBytes(subtype)
	}
	e.addBytes(v...)
}

func (e *encoder) addStr(v string) {
	e.addInt32(int32(len(v) + 1))
	e.addCStr(v)
}

func (e *encoder) addCStr(v string) {
	e.addBytes([]byte(v)...)
	e.addBytes(0)
}

func (e *encoder) reserveInt32() (pos int) {
	pos = len(e.out)
	e.addBytes(0, 0, 0, 0)
	return pos
}

func (e *encoder) setInt32(pos int, v int32) {
	e.out[pos+0] = byte(v)
	e.out[pos+1] = byte(v >> 8)
	e.out[pos+2] = byte(v >> 16)
	e.out[pos+3] = byte(v >> 24)
}

func (e *encoder) addInt32(v int32) {
	u := uint32(v)
	e.addBytes(byte(u), byte(u>>8), byte(u>>16), byte(u>>24))
}

func (e *encoder) addInt64(v int64) {
	u := uint64(v)
	e.addBytes(byte(u), byte(u>>8), byte(u>>16), byte(u>>24),
		byte(u>>32), byte(u>>40), byte(u>>48), byte(u>>56))
}

func (e *encoder) addFloat64(v float64) {
	e.addInt64(int64(math.Float64bits(v)))
}

func (e *encoder) addBytes(v ...byte) {
	e.out = append(e.out, v...)
}
