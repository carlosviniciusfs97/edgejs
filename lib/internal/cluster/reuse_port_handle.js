'use strict';

const {
  SafeMap,
} = primordials;

const assert = require('internal/assert');

module.exports = ReusePortHandle;

// WASIX cluster scheduling strategy: listen handles cannot be passed between
// processes (no SCM_RIGHTS over the IPC channel), but SO_REUSEPORT is fully
// supported and the host kernel balances connections between listeners. The
// primary therefore binds nothing; each worker is told (via the reusePort
// reply flag) to create its own listen handle with UV_TCP_REUSEPORT.
function ReusePortHandle(key, address, message) {
  this.key = key;
  this.workers = new SafeMap();
  this.errno = 0;
}

ReusePortHandle.prototype.add = function(worker, send) {
  assert(!this.workers.has(worker.id));
  this.workers.set(worker.id, worker);
  send(this.errno, { reusePort: true }, null);
};

ReusePortHandle.prototype.remove = function(worker) {
  if (!this.workers.has(worker.id))
    return false;

  this.workers.delete(worker.id);
  return this.workers.size === 0;
};

ReusePortHandle.prototype.has = function(worker) {
  return this.workers.has(worker.id);
};
