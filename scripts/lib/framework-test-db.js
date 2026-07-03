'use strict';

// Ephemeral database provisioning for framework tests.
//
// Backend apps declare their database need in a top-level `database` block in
// routes.json:
//
//   {
//     "version": 1,
//     "database": {
//       "kind": "postgres",
//       "env": {
//         "CMD_DB_URL": "{dbUrl}",
//         "CMD_PORT": "{port}"
//       }
//     },
//     "routes": [...]
//   }
//
// The harness then starts a real database as a plain child process before the
// app's server starts (no Docker: framework tests also run on macOS CI
// runners, which have no Docker daemon) and injects the `env` block into the
// server environment. Values may reference:
//   {dbUrl} {dbHost} {dbPort} {dbUser} {dbPassword} {dbName}  — database info
//   {port}                                                     — the app port,
//     expanded later by makeProjectEnv once the harness picks it.
//
// Postgres is provided by the `embedded-postgres` npm package (real zonky.io
// binaries spawned via initdb/pg_ctl), installed on demand into
// <stateDir>/db-tools with the same pnpm store the project installs use.

const fs = require('node:fs');
const net = require('node:net');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { createRequire } = require('node:module');

const EMBEDDED_POSTGRES_VERSION = '17.10.0-beta.17';
const DB_USER = 'framework';
const DB_PASSWORD = 'framework';
const DB_NAME = 'app';
const SUPPORTED_KINDS = ['postgres'];

const activeDatabases = new Set();
let signalHandlersInstalled = false;

function readProjectDatabaseConfig(project, routesJsonPath) {
  if (!fs.existsSync(routesJsonPath)) {
    return null;
  }

  let config = null;
  try {
    config = JSON.parse(fs.readFileSync(routesJsonPath, 'utf8'));
  } catch (error) {
    throw new Error(`invalid JSON in ${routesJsonPath}: ${error.message}`);
  }

  if (!config || typeof config !== 'object' || config.database == null) {
    return null;
  }

  const database = config.database;
  if (typeof database !== 'object') {
    throw new Error(`invalid database block in ${routesJsonPath}: expected an object`);
  }
  if (!SUPPORTED_KINDS.includes(database.kind)) {
    throw new Error(`invalid database block in ${routesJsonPath}: kind must be one of ${SUPPORTED_KINDS.join(', ')}`);
  }
  if (database.env != null && (typeof database.env !== 'object' || Array.isArray(database.env))) {
    throw new Error(`invalid database block in ${routesJsonPath}: env must be an object of string values`);
  }

  const env = {};
  for (const [name, value] of Object.entries(database.env || {})) {
    if (typeof value !== 'string') {
      throw new Error(`invalid database env value for ${name} in ${routesJsonPath}: expected a string`);
    }
    env[name] = value;
  }

  return { kind: database.kind, env };
}

function ensureDatabaseTools(stateDir, pnpmStoreDir, log) {
  const toolsDir = path.join(stateDir, 'db-tools');
  const manifestPath = path.join(toolsDir, 'package.json');
  const manifest = {
    name: 'framework-test-db-tools',
    private: true,
    dependencies: {
      'embedded-postgres': EMBEDDED_POSTGRES_VERSION,
    },
  };
  const manifestJson = `${JSON.stringify(manifest, null, 2)}\n`;

  fs.mkdirSync(toolsDir, { recursive: true });
  const existing = fs.existsSync(manifestPath) ? fs.readFileSync(manifestPath, 'utf8') : null;
  if (existing !== manifestJson) {
    fs.writeFileSync(manifestPath, manifestJson);
  }

  const installedMarker = path.join(toolsDir, 'node_modules', 'embedded-postgres', 'package.json');
  let needsInstall = existing !== manifestJson || !fs.existsSync(installedMarker);
  if (!needsInstall) {
    try {
      const installed = JSON.parse(fs.readFileSync(installedMarker, 'utf8'));
      needsInstall = installed.version !== EMBEDDED_POSTGRES_VERSION;
    } catch {
      needsInstall = true;
    }
  }

  if (needsInstall) {
    log(`installing database tools (embedded-postgres ${EMBEDDED_POSTGRES_VERSION}) into ${toolsDir}`);
    const args = ['install', '--no-lockfile', '--config.dangerouslyAllowAllBuilds=true'];
    if (pnpmStoreDir) {
      args.push('--store-dir', pnpmStoreDir);
    }
    const result = spawnSync('pnpm', args, { cwd: toolsDir, encoding: 'utf8' });
    if (result.status !== 0) {
      const detail = `${result.stdout || ''}${result.stderr || ''}`.trim();
      throw new Error(`pnpm install failed for database tools in ${toolsDir}${detail ? `:\n${detail}` : ''}`);
    }
  }

  return createRequire(manifestPath);
}

function allocateFreePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.unref();
    server.on('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const { port } = server.address();
      server.close((error) => (error ? reject(error) : resolve(port)));
    });
  });
}

function installSignalHandlers() {
  if (signalHandlersInstalled) {
    return;
  }
  signalHandlersInstalled = true;
  for (const signal of ['SIGINT', 'SIGTERM']) {
    process.on(signal, () => {
      stopAllDatabases().finally(() => {
        process.exit(signal === 'SIGINT' ? 130 : 143);
      });
    });
  }
}

async function startProjectDatabase(options) {
  const { config, project, stage, stateDir, pnpmStoreDir, log, logWarn } = options;
  if (config.kind !== 'postgres') {
    throw new Error(`unsupported database kind: ${config.kind}`);
  }

  const requireTools = ensureDatabaseTools(stateDir, pnpmStoreDir, log);
  const embeddedPostgres = requireTools('embedded-postgres');
  const EmbeddedPostgres = embeddedPostgres.default || embeddedPostgres;

  const port = await allocateFreePort();
  const dataDir = path.join(stateDir, 'db', `${project.name}.${stage.key}`);
  fs.rmSync(dataDir, { recursive: true, force: true });
  fs.mkdirSync(dataDir, { recursive: true });

  const instance = new EmbeddedPostgres({
    databaseDir: dataDir,
    user: DB_USER,
    password: DB_PASSWORD,
    port,
    persistent: false,
    onLog: () => {},
    onError: (message) => {
      logWarn(`postgres (${project.name}): ${String(message).trim()}`);
    },
  });

  await instance.initialise();
  await instance.start();
  await instance.createDatabase(DB_NAME);

  const values = {
    dbHost: '127.0.0.1',
    dbPort: String(port),
    dbUser: DB_USER,
    dbPassword: DB_PASSWORD,
    dbName: DB_NAME,
    dbUrl: `postgres://${DB_USER}:${DB_PASSWORD}@127.0.0.1:${port}/${DB_NAME}`,
  };

  const env = {};
  for (const [name, template] of Object.entries(config.env)) {
    env[name] = Object.entries(values).reduce(
      (value, [key, replacement]) => value.split(`{${key}}`).join(replacement),
      template,
    );
  }
  // The WASIX framework runner forwards only an allowlist of env vars into
  // the guest; it extends that allowlist with the names listed here.
  env.FRAMEWORK_TEST_EXTRA_ENV = Object.keys(env).join(',');

  const handle = {
    dataDir,
    env,
    kind: config.kind,
    port,
    stopped: false,
    async stop() {
      if (handle.stopped) {
        return;
      }
      handle.stopped = true;
      activeDatabases.delete(handle);
      try {
        await instance.stop();
      } catch (error) {
        logWarn(`failed to stop postgres for ${project.name}: ${error.message}`);
      }
      fs.rmSync(dataDir, { recursive: true, force: true });
    },
  };

  activeDatabases.add(handle);
  installSignalHandlers();
  return handle;
}

async function stopAllDatabases() {
  const handles = Array.from(activeDatabases);
  await Promise.allSettled(handles.map((handle) => handle.stop()));
}

module.exports = {
  readProjectDatabaseConfig,
  startProjectDatabase,
  stopAllDatabases,
};
