// Copyright (c) 2011 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2009-2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "drag_download_file.h"
#include "browser_impl.h"
#include "cef_thread.h"

#include "base/file_util.h"
#include "base/message_loop.h"
#include "net/base/file_stream.h"

DragDownloadFile::DragDownloadFile(
    const FilePath& file_name_or_path,
    linked_ptr<net::FileStream> file_stream,
    const GURL& url,
    const GURL& referrer,
    const std::string& referrer_encoding,
    BrowserWebViewDelegate* view)
    : file_stream_(file_stream),
      url_(url),
      referrer_(referrer),
      referrer_encoding_(referrer_encoding),
      view_(view),
      drag_message_loop_(MessageLoop::current()),
      is_started_(false),
      is_successful_(false) {
#if defined(OS_WIN)
  DCHECK(!file_name_or_path.empty() && !file_stream.get());
  file_name_ = file_name_or_path;
#elif defined(OS_POSIX)
  DCHECK(!file_name_or_path.empty() && file_stream.get());
  file_path_ = file_name_or_path;
#endif
}

DragDownloadFile::~DragDownloadFile() {
  AssertCurrentlyOnDragThread();

  // Since the target application can still hold and use the dragged file,
  // we do not know the time that it can be safely deleted. To solve this
  // problem, we schedule it to be removed after the system is restarted.
#if defined(OS_WIN)
  if (!temp_dir_path_.empty()) {
    if (!file_path_.empty())
      file_util::DeleteAfterReboot(file_path_);
    file_util::DeleteAfterReboot(temp_dir_path_);
  }
#endif
}

bool DragDownloadFile::Start(ui::DownloadFileObserver* observer) {
  AssertCurrentlyOnDragThread();

  if (is_started_)
    return true;
  is_started_ = true;

  DCHECK(!observer_.get());
  observer_ = observer;

  if (!file_stream_.get()) {
    // Create a temporary directory to save the temporary download file. We do
    // not want to use the default download directory since we do not want the
    // twisted file name shown in the download shelf if the file with the same
    // name already exists.
    if (!file_util::CreateNewTempDirectory(FILE_PATH_LITERAL("chrome"),
                                           &temp_dir_path_))
      return false;

    file_path_ = temp_dir_path_.Append(file_name_);
  }

  InitiateDownload();

  // On Windows, we need to wait till the download file is completed.
#if defined(OS_WIN)
  StartNestedMessageLoop();
#endif

  return is_successful_;
}

void DragDownloadFile::Stop() {
}

void DragDownloadFile::InitiateDownload() {
#if defined(OS_WIN)
  // DownloadManager could only be invoked from the UI thread.
  if (!CefThread::CurrentlyOn(CefThread::UI)) {
    CefThread::PostTask(
        CefThread::UI, FROM_HERE,
        NewRunnableMethod(this,
                          &DragDownloadFile::InitiateDownload));
    return;
  }
#endif

  // TODO: You seem to have found an example of HTML5 drag and drop download.
  // Please report it to the CEF developers so that we can add support for it.
  NOTREACHED();
  bool is_successful = false;
  DownloadCompleted(is_successful);
}

void DragDownloadFile::DownloadCompleted(bool is_successful) {
#if defined(OS_WIN)
  // If not in drag-and-drop thread, defer the running to it.
  if (drag_message_loop_ != MessageLoop::current()) {
    drag_message_loop_->PostTask(
        FROM_HERE,
        NewRunnableMethod(this,
                          &DragDownloadFile::DownloadCompleted,
                          is_successful));
    return;
  }
#endif

  is_successful_ = is_successful;

  // On Windows, we need to stop the waiting.
#if defined(OS_WIN)
  QuitNestedMessageLoop();
#endif
}

void DragDownloadFile::AssertCurrentlyOnDragThread() {
  // Only do the check on Windows where two threads are involved.
#if defined(OS_WIN)
  DCHECK(drag_message_loop_ == MessageLoop::current());
#endif
}

void DragDownloadFile::AssertCurrentlyOnUIThread() {
  // Only do the check on Windows where two threads are involved.
#if defined(OS_WIN)
  DCHECK(CefThread::CurrentlyOn(CefThread::UI));
#endif
}

#if defined(OS_WIN)
void DragDownloadFile::StartNestedMessageLoop() {
  AssertCurrentlyOnDragThread();

  bool old_state = MessageLoop::current()->NestableTasksAllowed();
  MessageLoop::current()->SetNestableTasksAllowed(true);
  is_running_nested_message_loop_ = true;
  MessageLoop::current()->Run();
  MessageLoop::current()->SetNestableTasksAllowed(old_state);
}

void DragDownloadFile::QuitNestedMessageLoop() {
  AssertCurrentlyOnDragThread();

  if (is_running_nested_message_loop_) {
    is_running_nested_message_loop_ = false;
    MessageLoop::current()->Quit();
  }
}
#endif
