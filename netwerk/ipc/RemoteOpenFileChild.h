/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _RemoteOpenFileChild_h
#define _RemoteOpenFileChild_h

#include "mozilla/dom/TabChild.h"
#include "mozilla/net/PRemoteOpenFileChild.h"
#include "nsILocalFile.h"
#include "nsIRemoteOpenFileListener.h"

namespace mozilla {
namespace net {

/**
 * RemoteOpenFileChild: a thin wrapper around regular nsIFile classes that does
 * IPC to open a file handle on parent instead of child.  Used when we can't
 * open file handle on child (don't have permission), but we don't want the
 * overhead of shipping all I/O traffic across IPDL.  Example: JAR files.
 *
 * To open a file handle with this class, AsyncRemoteFileOpen() must be called
 * first.  After the listener's OnRemoteFileOpenComplete() is called, if the
 * result is NS_OK, nsIFile.OpenNSPRFileDesc() may be called--once--to get the
 * file handle.
 *
 * Note that calling Clone() on this class results in the filehandle ownership
 * being passed on to the new RemoteOpenFileChild. I.e. if
 * OnRemoteFileOpenComplete is called and then Clone(), OpenNSPRFileDesc() will
 * work in the cloned object, but not in the original.
 *
 * This class should only be instantiated in a child process.
 *
 */
class RemoteOpenFileChild MOZ_FINAL
  : public PRemoteOpenFileChild
  , public nsIFile
  , public nsIHashable
{
public:
  RemoteOpenFileChild()
    : mNSPRFileDesc(nullptr)
    , mAsyncOpenCalled(false)
    , mNSPROpenCalled(false)
  {}

  virtual ~RemoteOpenFileChild();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIFILE
  NS_DECL_NSIHASHABLE

  // URI must be scheme 'remoteopenfile://': otherwise looks like a file:// uri.
  nsresult Init(nsIURI* aRemoteOpenUri);

  void AddIPDLReference();
  void ReleaseIPDLReference();

  // Send message to parent to tell it to open file handle for file.
  // TabChild is required, for IPC security.
  // Note: currently only PR_RDONLY is supported for 'flags'
  nsresult AsyncRemoteFileOpen(int32_t aFlags,
                               nsIRemoteOpenFileListener* aListener,
                               nsITabChild* aTabChild);
private:
  RemoteOpenFileChild(const RemoteOpenFileChild& other);

protected:
  virtual bool RecvFileOpened(const FileDescriptor&);
  virtual bool RecvFileDidNotOpen();

  // regular nsIFile object, that we forward most calls to.
  nsCOMPtr<nsIFile> mFile;
  nsCOMPtr<nsIURI> mURI;
  nsCOMPtr<nsIRemoteOpenFileListener> mListener;
  PRFileDesc* mNSPRFileDesc;
  bool mAsyncOpenCalled;
  bool mNSPROpenCalled;
};

} // namespace net
} // namespace mozilla

#endif // _RemoteOpenFileChild_h

