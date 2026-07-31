'use strict'

// WASIX currently reports ENOSYS for descriptor timestamp updates. pnpm uses
// futimes only as a best-effort store touch, so contain the fallback to this
// command instead of changing Node's fs semantics for every EdgeJS program.
const fs = require('fs')
const trace = process.env.EDGE_TRACE_PNPM_FS === '1'

function noOpOwnershipChange(...args) {
  const callback = args.at(-1)
  if (typeof callback === 'function') process.nextTick(callback, null)
}

function noOpOwnershipChangeSync() {}

async function noOpOwnershipChangePromise() {}

// WASIX aborts the guest on chown rather than returning a catchable ENOSYS.
// pnpm only requests these calls to preserve ownership on atomic rewrites.
for (const name of ['chown', 'fchown', 'lchown']) {
  if (typeof fs[name] === 'function') fs[name] = noOpOwnershipChange
  const syncName = `${name}Sync`
  if (typeof fs[syncName] === 'function') fs[syncName] = noOpOwnershipChangeSync
  if (typeof fs.promises[name] === 'function') {
    fs.promises[name] = noOpOwnershipChangePromise
  }
}

const originalFutimes = fs.futimes
const originalFutimesSync = fs.futimesSync

fs.futimes = function futimesWithWasixFallback(fd, atime, mtime, callback) {
  return originalFutimes.call(fs, fd, atime, mtime, (error) => {
    if (error?.code === 'ENOSYS') {
      if (trace) console.error('[edge-pnpm] ignored futimes ENOSYS')
      callback(null)
    } else {
      callback(error)
    }
  })
}

fs.futimesSync = function futimesSyncWithWasixFallback(fd, atime, mtime) {
  try {
    return originalFutimesSync.call(fs, fd, atime, mtime)
  } catch (error) {
    if (error?.code !== 'ENOSYS') throw error
    if (trace) console.error('[edge-pnpm] ignored futimesSync ENOSYS')
  }
}

const originalRename = fs.rename
const originalRenameSync = fs.renameSync
const originalPromisesRename = fs.promises.rename

fs.rename = function renameWithWasixFileFallback(oldPath, newPath, callback) {
  fs.lstat(oldPath, (statError, stat) => {
    if (statError || !stat.isFile()) {
      return originalRename.call(fs, oldPath, newPath, callback)
    }
    if (trace) console.error('[edge-pnpm] copy/unlink rename', oldPath, newPath)
    fs.copyFile(oldPath, newPath, (copyError) => {
      if (copyError) return callback(copyError)
      fs.unlink(oldPath, callback)
    })
  })
}

fs.renameSync = function renameSyncWithWasixFileFallback(oldPath, newPath) {
  const stat = fs.lstatSync(oldPath)
  if (!stat.isFile()) return originalRenameSync.call(fs, oldPath, newPath)
  if (trace) console.error('[edge-pnpm] copy/unlink renameSync', oldPath, newPath)
  fs.copyFileSync(oldPath, newPath)
  fs.unlinkSync(oldPath)
}

fs.promises.rename = async function promisesRenameWithWasixFileFallback(
  oldPath,
  newPath,
) {
  const stat = await fs.promises.lstat(oldPath)
  if (!stat.isFile()) return originalPromisesRename.call(fs.promises, oldPath, newPath)
  if (trace) console.error('[edge-pnpm] copy/unlink promises.rename', oldPath, newPath)
  await fs.promises.copyFile(oldPath, newPath)
  await fs.promises.unlink(oldPath)
}
