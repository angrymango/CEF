// Copyright (c) 2008 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef _STREAM_IMPL_H
#define _STREAM_IMPL_H

#include "../include/cef.h"
#include <stdio.h>

// Implementation of CefStreamReader for files.
class CefFileReader : public CefThreadSafeBase<CefStreamReader>
{
public:
  CefFileReader(FILE* file, bool close);
  ~CefFileReader();

  virtual size_t Read(void* ptr, size_t size, size_t n);
  virtual int Seek(long offset, int whence);
  virtual long Tell();
  virtual int Eof();
	
protected:
  bool close_;
  FILE *file_;
};

// Implementation of CefStreamWriter for files.
class CefFileWriter : public CefThreadSafeBase<CefStreamWriter>
{
public:
  CefFileWriter(FILE* file, bool close);
  ~CefFileWriter();

  virtual size_t Write(const void* ptr, size_t size, size_t n);
  virtual int Seek(long offset, int whence);
  virtual long Tell();
  virtual int Flush();
	
protected:
  FILE *file_;
  bool close_;
};

// Implementation of CefStreamReader for byte buffers.
class CefBytesReader : public CefThreadSafeBase<CefStreamReader>
{
public:
  CefBytesReader(void* data, long datasize, bool copy);
  ~CefBytesReader();

  virtual size_t Read(void* ptr, size_t size, size_t n);
  virtual int Seek(long offset, int whence);
  virtual long Tell();
  virtual int Eof();
	
  void SetData(void* data, long datasize, bool copy);

  void* GetData() { return data_; }
  size_t GetDataSize() { return offset_; }

protected:
	void* data_;
	size_t datasize_;
  bool copy_;
	size_t offset_;
};

// Implementation of CefStreamWriter for byte buffers.
class CefBytesWriter : public CefThreadSafeBase<CefStreamWriter>
{
public:
  CefBytesWriter(size_t grow);
  ~CefBytesWriter();

  virtual size_t Write(const void* ptr, size_t size, size_t n);
  virtual int Seek(long offset, int whence);
  virtual long Tell();
  virtual int Flush();

  void* GetData() { return data_; }
	size_t GetDataSize() { return offset_; }
  std::string GetDataString();
	
protected:
	size_t Grow(size_t size);

protected:
  size_t grow_;
  void* data_;
  size_t datasize_;
  size_t offset_;
};

#endif // _STREAM_IMPL_H
