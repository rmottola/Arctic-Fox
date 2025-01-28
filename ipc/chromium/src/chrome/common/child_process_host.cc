/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/child_process_host.h"

#include "base/compiler_specific.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/process_util.h"
#include "base/singleton.h"
#include "base/waitable_event.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/BrowserProcessSubThread.h"
#include "mozilla/ipc/Transport.h"
typedef mozilla::ipc::BrowserProcessSubThread ChromeThread;
#include "chrome/common/process_watcher.h"
#include "chrome/common/result_codes.h"

using mozilla::ipc::FileDescriptor;

namespace {
typedef std::list<ChildProcessHost*> ChildProcessList;
}  // namespace



ChildProcessHost::ChildProcessHost(ProcessType type)
    :
      ChildProcessInfo(type),
      ALLOW_THIS_IN_INITIALIZER_LIST(listener_(this)),
      opening_channel_(false),
      process_event_(nullptr) {
  Singleton<ChildProcessList>::get()->push_back(this);
}


ChildProcessHost::~ChildProcessHost() {
  Singleton<ChildProcessList>::get()->remove(this);

  if (handle()) {
    watcher_.StopWatching();

#if defined(OS_WIN)
    // Above call took ownership, so don't want WaitableEvent to assert because
    // the handle isn't valid anymore.
    process_event_->Release();
#endif
  }
}

bool ChildProcessHost::CreateChannel() {
  channel_id_ = IPC::Channel::GenerateVerifiedChannelID(std::wstring());
  channel_.reset(new IPC::Channel(
      channel_id_, IPC::Channel::MODE_SERVER, &listener_));
  if (!channel_->Connect())
    return false;

  opening_channel_ = true;

  return true;
}

bool ChildProcessHost::CreateChannel(FileDescriptor& aFileDescriptor) {
  if (channel_.get()) {
    channel_->Close();
  }
  channel_ = mozilla::ipc::OpenDescriptor(aFileDescriptor, IPC::Channel::MODE_SERVER);
  if (!channel_->Connect()) {
    return false;
  }

  opening_channel_ = true;

  return true;
}

void ChildProcessHost::SetHandle(base::ProcessHandle process) {
#if defined(OS_WIN)
  process_event_.reset(new base::WaitableEvent(process));

  DCHECK(!handle());
  set_handle(process);
  watcher_.StartWatching(process_event_.get(), this);
#endif
}

bool ChildProcessHost::Send(IPC::Message* msg) {
  if (!channel_.get()) {
    delete msg;
    return false;
  }
  return channel_->Send(msg);
}

void ChildProcessHost::OnWaitableEventSignaled(base::WaitableEvent *event) {
}

ChildProcessHost::ListenerHook::ListenerHook(ChildProcessHost* host)
    : host_(host) {
}

void ChildProcessHost::ListenerHook::OnMessageReceived(
    IPC::Message&& msg) {
  host_->OnMessageReceived(mozilla::Move(msg));
}

void ChildProcessHost::ListenerHook::OnChannelConnected(int32_t peer_pid) {
  host_->opening_channel_ = false;
  host_->OnChannelConnected(peer_pid);
}

void ChildProcessHost::ListenerHook::OnChannelError() {
  host_->opening_channel_ = false;
  host_->OnChannelError();
}

void ChildProcessHost::ListenerHook::GetQueuedMessages(std::queue<IPC::Message>& queue) {
  host_->GetQueuedMessages(queue);
}

ChildProcessHost::Iterator::Iterator() : all_(true) {
  iterator_ = Singleton<ChildProcessList>::get()->begin();
}

ChildProcessHost::Iterator::Iterator(ProcessType type)
    : all_(false), type_(type) {
  iterator_ = Singleton<ChildProcessList>::get()->begin();
  if (!Done() && (*iterator_)->type() != type_)
    ++(*this);
}

ChildProcessHost* ChildProcessHost::Iterator::operator++() {
  do {
    ++iterator_;
    if (Done())
      break;

    if (!all_ && (*iterator_)->type() != type_)
      continue;

    return *iterator_;
  } while (true);

  return NULL;
}

bool ChildProcessHost::Iterator::Done() {
  return iterator_ == Singleton<ChildProcessList>::get()->end();
}
