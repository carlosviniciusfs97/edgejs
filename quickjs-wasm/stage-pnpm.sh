#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/pnpm"

PNPM_VERSION="10.34.5"
PNPM_ARCHIVE_URL="https://registry.npmjs.org/pnpm/-/pnpm-${PNPM_VERSION}.tgz"
# npm dist.integrity:
# sha512-pO4F8vc2WCVb1qiYWcBlpFwopX2u+uLIk6Fo7itzFow3uR6D5X6mdlStA/AwMXRkMOi84442LgQmBfuKvIAZLg==
PNPM_ARCHIVE_SHA512="a4ee05f2f73658255bd6a89859c065a45c28a57daefae2c893a168ee2b73168c37b91e83e57ea67654ad03f03031746430e8bce38e362e042605fb8abc80192e"
PNPM_BUNDLE_SHA256="ca969fbb32c08be8395c744c64e131c2ba2c93067e1b483171dbc6ed439e1143"
PNPM_WORKER_SHA256="7b8aba01c22e65e9d1998fd1e30dc9722e4c315f80c87bcc54826d9ddb944e59"
PNPM_LICENSE_SHA256="e0a867ff513ea7be2a0ddc339ac6a031e459a38668e077b8f0e649544062f9f2"
PNPM_PACKAGE_JSON_SHA256="e71ff1704fb0e59fed11deae60a52095b128a9286f632b0feda7dd04d033fe3c"

hash_file() {
  local algorithm="$1"
  local path="$2"

  if command -v "sha${algorithm}sum" >/dev/null 2>&1; then
    "sha${algorithm}sum" "${path}" | awk '{print $1}'
    return
  fi
  if command -v shasum >/dev/null 2>&1; then
    shasum -a "${algorithm}" "${path}" | awk '{print $1}'
    return
  fi

  echo "error: sha${algorithm}sum or shasum is required" >&2
  return 1
}

check_file() {
  local path="$1"
  local algorithm="$2"
  local expected="$3"

  [[ -f "${path}" ]] && [[ "$(hash_file "${algorithm}" "${path}")" == "${expected}" ]]
}

if check_file "${OUTPUT_DIR}/pnpm.cjs" 256 "${PNPM_BUNDLE_SHA256}" && \
   check_file "${OUTPUT_DIR}/worker.bundle.js" 256 "${PNPM_WORKER_SHA256}" && \
   check_file "${OUTPUT_DIR}/LICENSE" 256 "${PNPM_LICENSE_SHA256}" && \
   check_file "${OUTPUT_DIR}/package.json" 256 "${PNPM_PACKAGE_JSON_SHA256}"; then
  echo "pnpm ${PNPM_VERSION} is already staged at ${OUTPUT_DIR}/pnpm.cjs"
  exit 0
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "error: curl is required to stage pnpm" >&2
  exit 1
fi
if ! command -v tar >/dev/null 2>&1; then
  echo "error: tar is required to stage pnpm" >&2
  exit 1
fi

STAGING_DIR="$(mktemp -d "${TMPDIR:-/tmp}/edgejs-pnpm.XXXXXX")"
trap 'rm -rf "${STAGING_DIR}"' EXIT

ARCHIVE="${STAGING_DIR}/pnpm-${PNPM_VERSION}.tgz"
curl --fail --location --retry 3 --silent --show-error \
  --output "${ARCHIVE}" \
  "${PNPM_ARCHIVE_URL}"

actual_archive_sha512="$(hash_file 512 "${ARCHIVE}")"
if [[ "${actual_archive_sha512}" != "${PNPM_ARCHIVE_SHA512}" ]]; then
  echo "error: pnpm ${PNPM_VERSION} archive integrity mismatch" >&2
  echo "expected: ${PNPM_ARCHIVE_SHA512}" >&2
  echo "actual:   ${actual_archive_sha512}" >&2
  exit 1
fi

tar -xOf "${ARCHIVE}" package/dist/pnpm.cjs > "${STAGING_DIR}/pnpm.cjs"
tar -xOf "${ARCHIVE}" package/dist/worker.js > "${STAGING_DIR}/worker.bundle.js"
tar -xOf "${ARCHIVE}" package/LICENSE > "${STAGING_DIR}/LICENSE"
tar -xOf "${ARCHIVE}" package/package.json > "${STAGING_DIR}/package.json"

check_file "${STAGING_DIR}/pnpm.cjs" 256 "${PNPM_BUNDLE_SHA256}" || {
  echo "error: extracted pnpm.cjs checksum mismatch" >&2
  exit 1
}
check_file "${STAGING_DIR}/worker.bundle.js" 256 "${PNPM_WORKER_SHA256}" || {
  echo "error: extracted pnpm worker.js checksum mismatch" >&2
  exit 1
}
check_file "${STAGING_DIR}/LICENSE" 256 "${PNPM_LICENSE_SHA256}" || {
  echo "error: extracted pnpm LICENSE checksum mismatch" >&2
  exit 1
}
check_file "${STAGING_DIR}/package.json" 256 "${PNPM_PACKAGE_JSON_SHA256}" || {
  echo "error: extracted pnpm package.json checksum mismatch" >&2
  exit 1
}

mkdir -p "${OUTPUT_DIR}"
install -m 0644 "${STAGING_DIR}/pnpm.cjs" "${OUTPUT_DIR}/pnpm.cjs"
install -m 0644 "${STAGING_DIR}/worker.bundle.js" "${OUTPUT_DIR}/worker.bundle.js"
install -m 0644 "${STAGING_DIR}/LICENSE" "${OUTPUT_DIR}/LICENSE"
install -m 0644 "${STAGING_DIR}/package.json" "${OUTPUT_DIR}/package.json"

echo "Staged pnpm ${PNPM_VERSION} at ${OUTPUT_DIR}/pnpm.cjs"
