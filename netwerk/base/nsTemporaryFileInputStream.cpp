/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTemporaryFileInputStream.h"
#include "nsStreamUtils.h"
#include <algorithm>

NS_IMPL_ISUPPORTS(nsTemporaryFileInputStream, nsIInputStream, nsISeekableStream)

nsTemporaryFileInputStream::nsTemporaryFileInputStream(FileDescOwner* aFileDescOwner, uint64_t aStartPos, uint64_t aEndPos)
  : mFileDescOwner(aFileDescOwner),
    mStartPos(aStartPos),
    mCurPos(aStartPos),
    mEndPos(aEndPos),
    mClosed(false)
{ 
  NS_ASSERTION(aStartPos <= aEndPos, "StartPos should less equal than EndPos!");
}

NS_IMETHODIMP
nsTemporaryFileInputStream::Close()
{
  mClosed = true;
  return NS_OK;
}

NS_IMETHODIMP
nsTemporaryFileInputStream::Available(uint64_t * bytesAvailable)
{
  if (mClosed)
    return NS_BASE_STREAM_CLOSED;

  NS_ASSERTION(mCurPos <= mEndPos, "CurPos should less equal than EndPos!");

  *bytesAvailable = mEndPos - mCurPos;
  return NS_OK;
}

NS_IMETHODIMP
nsTemporaryFileInputStream::Read(char* buffer, uint32_t count, uint32_t* bytesRead)
{
  return ReadSegments(NS_CopySegmentToBuffer, buffer, count, bytesRead);
}

NS_IMETHODIMP
nsTemporaryFileInputStream::ReadSegments(nsWriteSegmentFun writer,
                                         void *            closure,
                                         uint32_t          count,
                                         uint32_t *        result)
{
  NS_ASSERTION(result, "null ptr");
  NS_ASSERTION(mCurPos <= mEndPos, "bad stream state");
  *result = 0;

  if (mClosed) {
    return NS_BASE_STREAM_CLOSED;
  }

  mozilla::MutexAutoLock lock(mFileDescOwner->FileMutex());
  int64_t offset = PR_Seek64(mFileDescOwner->mFD, mCurPos, PR_SEEK_SET);
  if (offset == -1) {
    return NS_ErrorAccordingToNSPR();
  }

  // Limit requested count to the amount remaining in our section of the file.
  count = std::min(count, uint32_t(mEndPos - mCurPos));

  char buf[4096];
  while (*result < count) {
    uint32_t bufCount = std::min(count - *result, (uint32_t) sizeof(buf));
    int32_t bytesRead = PR_Read(mFileDescOwner->mFD, buf, bufCount);
    if (bytesRead < 0) {
      return NS_ErrorAccordingToNSPR();
    }

    int32_t bytesWritten = 0;
    while (bytesWritten < bytesRead) {
      uint32_t writerCount = 0;
      nsresult rv = writer(this, closure, buf + bytesWritten, *result,
                           bytesRead - bytesWritten, &writerCount);
      if (NS_FAILED(rv) || writerCount == 0) {
        // nsIInputStream::ReadSegments' contract specifies that errors
        // from writer are not propagated to ReadSegments' caller.
        //
        // If writer fails, leaving bytes still in buf, that's okay: we
        // only update mCurPos to reflect successful writes, so the call
        // to PR_Seek64 at the top will restart us at the right spot.
        return NS_OK;
      }
      NS_ASSERTION(writerCount <= (uint32_t) (bytesRead - bytesWritten),
                   "writer should not write more than we asked it to write");
      bytesWritten += writerCount;
      *result += writerCount;
      mCurPos += writerCount;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsTemporaryFileInputStream::IsNonBlocking(bool * nonBlocking)
{
  *nonBlocking = false;
  return NS_OK;
}

NS_IMETHODIMP
nsTemporaryFileInputStream::Seek(int32_t aWhence, int64_t aOffset)
{
  if (mClosed) {
    return NS_BASE_STREAM_CLOSED;
  }

  switch (aWhence) {
    case nsISeekableStream::NS_SEEK_SET:
      aOffset += mStartPos;
      break;

    case nsISeekableStream::NS_SEEK_CUR:
      aOffset += mCurPos;
      break;

    case nsISeekableStream::NS_SEEK_END:
      aOffset += mEndPos;
      break;

    default:
      return NS_ERROR_FAILURE;
  }

  if (aOffset < (int64_t)mStartPos || aOffset > (int64_t)mEndPos) {
    return NS_ERROR_INVALID_ARG;
  }

  mCurPos = aOffset;
  return NS_OK;
}

NS_IMETHODIMP
nsTemporaryFileInputStream::Tell(int64_t* aPos)
{
  if (!aPos) {
    return NS_ERROR_FAILURE;
  }

  if (mClosed) {
    return NS_BASE_STREAM_CLOSED;
  }

  MOZ_ASSERT(mStartPos <= mCurPos, "StartPos should less equal than CurPos!");
  *aPos = mCurPos - mStartPos;
  return NS_OK;
}

NS_IMETHODIMP
nsTemporaryFileInputStream::SetEOF()
{
  if (mClosed) {
    return NS_BASE_STREAM_CLOSED;
  }

  return Close();
}
