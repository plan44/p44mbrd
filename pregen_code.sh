#!/usr/bin/env bash

# Details see: file:///Users/luz/Code/looking_at_external_stuff/connectedhomeip/docs/code_generation.md

# Prepare
CHIPAPP_ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
export CHIPAPP_NAME="p44mbrd"

cd "${CHIPAPP_ROOT}/src"
source third_party/connectedhomeip/scripts/activate.sh

# Step 1: generate .matter (from .zap)
third_party/connectedhomeip/scripts/tools/zap/generate.py ${CHIPAPP_ROOT}/src/zap/p44mbrd.zap

# Step 2: pre-generate code
# Note: not all of it, only our specific code.
#   SDK common generated code is available in the SDK already)
PREGEN_DIR="${CHIPAPP_ROOT}/src/pregen"
mkdir -p ${PREGEN_DIR}
third_party/connectedhomeip/scripts/codepregen.py --input-glob "*p44mbrd*"  --external-root "${CHIPAPP_ROOT}/src/zap" ${PREGEN_DIR}
