#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
source "$SCRIPT_DIR/common.sh"

BOARD_HOST="192.168.2.117"
BOARD_USER="pi"
BOARD_PASS="pi"
BOARD_DEB_PATH="~/buddy-robot_1.0.0_arm64_npu.deb"
BOARD_OUTPUT_BASE="~/output"
RUN_SECONDS="25"
BUILD_SERVICES="0"
ACTION_LIST="build,deploy,run,fetch-log"
DEPLOY_PARTS="runtime,models,services"
LOCAL_LOG_DIR="$ROOT_DIR/output/board_logs"

usage() {
    cat <<USAGE
Usage:
  ./scripts/deploy_run_arm64_npu.sh [options]

Core options:
  --host <ip>             Board IP/hostname (default: ${BOARD_HOST})
  --user <name>           SSH user (default: ${BOARD_USER})
  --password <pass>       SSH password (default: ${BOARD_PASS})
  --run-seconds <sec>     Run duration before auto-stop (default: ${RUN_SECONDS})
  --log-dir <dir>         Local log directory (default: ${LOCAL_LOG_DIR})
  -h, --help              Show this help

Action control (what to do):
  --actions <list>        Comma list: build,deploy,run,fetch-log
                          Default: ${ACTION_LIST}
                          Typical:
                            build,deploy,run,fetch-log   # full pipeline
                            deploy                        # deploy only
                            run,fetch-log                 # run existing board install

Deploy part control (what to deploy in deploy action):
  --parts <list>          Comma list: runtime,models,services | all
                          Default: ${DEPLOY_PARTS}
                          Typical:
                            runtime,models
                            services
                            all

Service packaging:
  --build-services        Build service tarballs before deploy

Compatibility (deprecated but still supported):
  --no-build --no-runtime --no-models --no-services --no-run --no-fetch-log

Examples:
  ./scripts/deploy_run_arm64_npu.sh
  ./scripts/deploy_run_arm64_npu.sh --actions deploy --parts runtime,models
  ./scripts/deploy_run_arm64_npu.sh --actions deploy --parts services --build-services
  ./scripts/deploy_run_arm64_npu.sh --actions run,fetch-log --run-seconds 40
USAGE
}

has_item() {
    local list="$1"
    local item="$2"
    [[ ",$list," == *",$item,"* ]]
}

normalize_list() {
    echo "$1" | tr '[:upper:]' '[:lower:]' | tr -d ' '
}

warn_deprecated() {
    local flag="$1"
    local replacement="$2"
    echo "  [..] Deprecated option $flag detected. Use: $replacement" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host)
            BOARD_HOST="$2"
            shift 2
            ;;
        --user)
            BOARD_USER="$2"
            shift 2
            ;;
        --password)
            BOARD_PASS="$2"
            shift 2
            ;;
        --run-seconds)
            RUN_SECONDS="$2"
            shift 2
            ;;
        --actions)
            ACTION_LIST="$(normalize_list "$2")"
            shift 2
            ;;
        --parts)
            DEPLOY_PARTS="$(normalize_list "$2")"
            shift 2
            ;;
        --build-services)
            BUILD_SERVICES="1"
            shift
            ;;
        --no-build)
            warn_deprecated "--no-build" "--actions deploy,run,fetch-log"
            ACTION_LIST="$(echo "$ACTION_LIST" | sed 's/\(^\|,\)build\(,\|$\)/\1\2/g; s/,,/,/g; s/^,//; s/,$//')"
            shift
            ;;
        --no-runtime)
            warn_deprecated "--no-runtime" "--parts models,services"
            DEPLOY_PARTS="$(echo "$DEPLOY_PARTS" | sed 's/\(^\|,\)runtime\(,\|$\)/\1\2/g; s/,,/,/g; s/^,//; s/,$//')"
            shift
            ;;
        --no-models)
            warn_deprecated "--no-models" "--parts runtime,services"
            DEPLOY_PARTS="$(echo "$DEPLOY_PARTS" | sed 's/\(^\|,\)models\(,\|$\)/\1\2/g; s/,,/,/g; s/^,//; s/,$//')"
            shift
            ;;
        --no-services)
            warn_deprecated "--no-services" "--parts runtime,models"
            DEPLOY_PARTS="$(echo "$DEPLOY_PARTS" | sed 's/\(^\|,\)services\(,\|$\)/\1\2/g; s/,,/,/g; s/^,//; s/,$//')"
            shift
            ;;
        --no-run)
            warn_deprecated "--no-run" "--actions build,deploy"
            ACTION_LIST="$(echo "$ACTION_LIST" | sed 's/\(^\|,\)run\(,\|$\)/\1\2/g; s/,,/,/g; s/^,//; s/,$//')"
            shift
            ;;
        --no-fetch-log)
            warn_deprecated "--no-fetch-log" "--actions build,deploy,run"
            ACTION_LIST="$(echo "$ACTION_LIST" | sed 's/\(^\|,\)fetch-log\(,\|$\)/\1\2/g; s/,,/,/g; s/^,//; s/,$//')"
            shift
            ;;
        --log-dir)
            LOCAL_LOG_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            log_err "Unknown arg: $1"
            usage
            exit 1
            ;;
    esac
done

ACTION_LIST="$(normalize_list "${ACTION_LIST:-}")"
DEPLOY_PARTS="$(normalize_list "${DEPLOY_PARTS:-}")"
if [[ "$DEPLOY_PARTS" == "all" ]]; then
    DEPLOY_PARTS="runtime,models,services"
fi

for act in ${ACTION_LIST//,/ }; do
    case "$act" in
        build|deploy|run|fetch-log|"") ;;
        *) log_err "Invalid action: $act"; exit 1 ;;
    esac
done
for part in ${DEPLOY_PARTS//,/ }; do
    case "$part" in
        runtime|models|services|"") ;;
        *) log_err "Invalid deploy part: $part"; exit 1 ;;
    esac
done

DO_BUILD="0"; has_item "$ACTION_LIST" "build" && DO_BUILD="1"
DO_DEPLOY="0"; has_item "$ACTION_LIST" "deploy" && DO_DEPLOY="1"
DO_RUN="0"; has_item "$ACTION_LIST" "run" && DO_RUN="1"
DO_FETCH_LOG="0"; has_item "$ACTION_LIST" "fetch-log" && DO_FETCH_LOG="1"

DO_DEPLOY_RUNTIME="0"; has_item "$DEPLOY_PARTS" "runtime" && DO_DEPLOY_RUNTIME="1"
DO_DEPLOY_MODELS="0"; has_item "$DEPLOY_PARTS" "models" && DO_DEPLOY_MODELS="1"
DO_DEPLOY_SERVICES="0"; has_item "$DEPLOY_PARTS" "services" && DO_DEPLOY_SERVICES="1"

if [[ "$DO_DEPLOY" == "0" ]]; then
    DO_DEPLOY_RUNTIME="0"
    DO_DEPLOY_MODELS="0"
    DO_DEPLOY_SERVICES="0"
fi

if ! command -v sshpass >/dev/null 2>&1; then
    log_err "sshpass not found. Install it first."
    exit 1
fi

if ! [[ "$RUN_SECONDS" =~ ^[0-9]+$ ]] || [[ "$RUN_SECONDS" -lt 1 ]]; then
    log_err "--run-seconds must be a positive integer"
    exit 1
fi

DEB_PATH="$ROOT_DIR/output/aarch64/deb/buddy-robot_1.0.0_arm64_npu.deb"
MODELS_GLOB="$ROOT_DIR/output/aarch64/deb/buddy-models_*_npu.tar.gz"
MODELS_PATH="$(ls -t $MODELS_GLOB 2>/dev/null | head -1 || true)"
MODELS_REMOTE_PATH="~/buddy-models_npu.tar.gz"
MODELS_REMOTE_SHA_PATH="~/buddy-models_npu.sha256"
MODELS_REMOTE_CACHE_DIR="${BOARD_OUTPUT_BASE}/buddy-models-cache"
DEB_REMOTE_SHA_PATH="~/buddy-runtime.sha256"

SVC_DIR="$ROOT_DIR/output/aarch64/services"
SVC_LLM_PATH="$(ls -t "$SVC_DIR"/buddy-service-llm_*_aarch64.tar.gz 2>/dev/null | head -1 || true)"
SVC_FUNASR_PATH="$(ls -t "$SVC_DIR"/buddy-service-funasr_*_aarch64.tar.gz 2>/dev/null | head -1 || true)"
SVC_CHATTTS_PATH="$(ls -t "$SVC_DIR"/buddy-service-chattts_*_aarch64.tar.gz 2>/dev/null | head -1 || true)"
SVC_REMOTE_BASE="~/buddy-services"
SVC_CACHE_BASE="${BOARD_OUTPUT_BASE}/buddy-services-cache"
mkdir -p "$LOCAL_LOG_DIR"

SSH_BASE=(sshpass -p "$BOARD_PASS" ssh -o StrictHostKeyChecking=no -o ConnectTimeout=10 "${BOARD_USER}@${BOARD_HOST}")
SCP_BASE=(sshpass -p "$BOARD_PASS" scp -o StrictHostKeyChecking=no)

log_stage "Step 1/5: Build artifacts"
if [[ "$DO_BUILD" == "1" ]]; then
    log_step "Running ./build.sh -t arm64 -d npu"
    (
        cd "$ROOT_DIR"
        ./build.sh -t arm64 -d npu
    )
    log_ok "Build finished"
else
    log_skip "Build"
fi

if [[ "$BUILD_SERVICES" == "1" ]]; then
    log_step "Running ./scripts/package_services.sh --arch arm64"
    (
        cd "$ROOT_DIR"
        ./scripts/package_services.sh --arch arm64
    )
    SVC_LLM_PATH="$(ls -t "$SVC_DIR"/buddy-service-llm_*_aarch64.tar.gz 2>/dev/null | head -1 || true)"
    SVC_FUNASR_PATH="$(ls -t "$SVC_DIR"/buddy-service-funasr_*_aarch64.tar.gz 2>/dev/null | head -1 || true)"
    SVC_CHATTTS_PATH="$(ls -t "$SVC_DIR"/buddy-service-chattts_*_aarch64.tar.gz 2>/dev/null | head -1 || true)"
    log_ok "Service packages finished"
fi

if [[ "$DO_DEPLOY_RUNTIME" == "1" && ! -f "$DEB_PATH" ]]; then
    log_err "Deb not found: $DEB_PATH"
    exit 1
fi

DEPLOY_RUNTIME="0"
DEPLOY_MODELS="0"
DEPLOY_SVC_LLM="0"
DEPLOY_SVC_FUNASR="0"
DEPLOY_SVC_CHATTTS="0"

if [[ "$DO_DEPLOY" == "1" ]]; then
    log_stage "Step 2/5: Upload changed artifacts"
    "${SSH_BASE[@]}" "mkdir -p ${BOARD_OUTPUT_BASE} ${SVC_REMOTE_BASE}"
    if [[ "$DO_DEPLOY_RUNTIME" == "1" ]]; then
        local_runtime_sha="$(sha256sum "$DEB_PATH" | awk '{print $1}')"
        remote_runtime_sha="$("${SSH_BASE[@]}" "cat ${DEB_REMOTE_SHA_PATH} 2>/dev/null || true")"
        if [[ "$local_runtime_sha" == "$remote_runtime_sha" ]]; then
            log_step "Runtime unchanged (sha256 match), skip deb upload"
        else
            DEPLOY_RUNTIME="1"
            log_step "Uploading runtime deb: $(basename "$DEB_PATH")"
            "${SCP_BASE[@]}" "$DEB_PATH" "${BOARD_USER}@${BOARD_HOST}:${BOARD_DEB_PATH}"
        fi
    else
        log_skip "Runtime deploy"
    fi

    if [[ "$DO_DEPLOY_MODELS" == "1" ]]; then
        if [[ -n "$MODELS_PATH" && -f "$MODELS_PATH" ]]; then
            local_models_sha="$(sha256sum "$MODELS_PATH" | awk '{print $1}')"
            remote_models_sha="$("${SSH_BASE[@]}" "cat ${MODELS_REMOTE_SHA_PATH} 2>/dev/null || true")"
            remote_models_ready="$("${SSH_BASE[@]}" "test -f ${MODELS_REMOTE_CACHE_DIR}/zipformer-rknn/tokens.txt && echo yes || echo no")"
            if [[ "$local_models_sha" == "$remote_models_sha" && "$remote_models_ready" == "yes" ]]; then
                log_step "Models unchanged (sha256 match), skip model upload"
            else
                DEPLOY_MODELS="1"
                log_step "Uploading updated models tar: $(basename "$MODELS_PATH")"
                "${SCP_BASE[@]}" "$MODELS_PATH" "${BOARD_USER}@${BOARD_HOST}:${MODELS_REMOTE_PATH}"
            fi
        else
            log_step "No models tarball found (glob: $MODELS_GLOB), keep board models as-is"
        fi
    else
        log_skip "Models deploy"
    fi

    if [[ "$DO_DEPLOY_SERVICES" == "1" ]]; then
        if [[ -n "$SVC_LLM_PATH" && -f "$SVC_LLM_PATH" ]]; then
            local_sha="$(sha256sum "$SVC_LLM_PATH" | awk '{print $1}')"
            remote_sha="$("${SSH_BASE[@]}" "cat ${SVC_REMOTE_BASE}/buddy-service-llm.sha256 2>/dev/null || true")"
            if [[ "$local_sha" == "$remote_sha" ]]; then
                log_step "Service llm unchanged, skip upload"
            else
                DEPLOY_SVC_LLM="1"
                log_step "Uploading service llm: $(basename "$SVC_LLM_PATH")"
                "${SCP_BASE[@]}" "$SVC_LLM_PATH" "${BOARD_USER}@${BOARD_HOST}:${SVC_REMOTE_BASE}/buddy-service-llm.tar.gz"
            fi
        fi
        if [[ -n "$SVC_FUNASR_PATH" && -f "$SVC_FUNASR_PATH" ]]; then
            local_sha="$(sha256sum "$SVC_FUNASR_PATH" | awk '{print $1}')"
            remote_sha="$("${SSH_BASE[@]}" "cat ${SVC_REMOTE_BASE}/buddy-service-funasr.sha256 2>/dev/null || true")"
            if [[ "$local_sha" == "$remote_sha" ]]; then
                log_step "Service funasr unchanged, skip upload"
            else
                DEPLOY_SVC_FUNASR="1"
                log_step "Uploading service funasr: $(basename "$SVC_FUNASR_PATH")"
                "${SCP_BASE[@]}" "$SVC_FUNASR_PATH" "${BOARD_USER}@${BOARD_HOST}:${SVC_REMOTE_BASE}/buddy-service-funasr.tar.gz"
            fi
        fi
        if [[ -n "$SVC_CHATTTS_PATH" && -f "$SVC_CHATTTS_PATH" ]]; then
            local_sha="$(sha256sum "$SVC_CHATTTS_PATH" | awk '{print $1}')"
            remote_sha="$("${SSH_BASE[@]}" "cat ${SVC_REMOTE_BASE}/buddy-service-chattts.sha256 2>/dev/null || true")"
            if [[ "$local_sha" == "$remote_sha" ]]; then
                log_step "Service chattts unchanged, skip upload"
            else
                DEPLOY_SVC_CHATTTS="1"
                log_step "Uploading service chattts: $(basename "$SVC_CHATTTS_PATH")"
                "${SCP_BASE[@]}" "$SVC_CHATTTS_PATH" "${BOARD_USER}@${BOARD_HOST}:${SVC_REMOTE_BASE}/buddy-service-chattts.tar.gz"
            fi
        fi
    else
        log_skip "Services deploy"
    fi
    log_ok "Upload stage complete"
else
    log_stage "Step 2/5: Upload changed artifacts"
    log_skip "Deploy action disabled"
fi

if [[ "$DO_DEPLOY" == "1" ]]; then
log_stage "Step 3/5: Apply runtime/models/services on board"
"${SSH_BASE[@]}" "set -euo pipefail; \
    DEPLOY_RUNTIME=${DEPLOY_RUNTIME}; \
    DEPLOY_MODELS=${DEPLOY_MODELS}; \
    DEPLOY_SVC_LLM=${DEPLOY_SVC_LLM}; \
    DEPLOY_SVC_FUNASR=${DEPLOY_SVC_FUNASR}; \
    DEPLOY_SVC_CHATTTS=${DEPLOY_SVC_CHATTTS}; \
    DEB_REMOTE_SHA_PATH=${DEB_REMOTE_SHA_PATH}; \
    MODELS_REMOTE_PATH=${MODELS_REMOTE_PATH}; \
    MODELS_REMOTE_SHA_PATH=${MODELS_REMOTE_SHA_PATH}; \
    MODELS_REMOTE_CACHE_DIR=${MODELS_REMOTE_CACHE_DIR}; \
    SVC_REMOTE_BASE=${SVC_REMOTE_BASE}; \
    SVC_CACHE_BASE=${SVC_CACHE_BASE}; \
    BOARD_DEB_PATH=${BOARD_DEB_PATH}; \
    mkdir -p ${BOARD_OUTPUT_BASE}; \
    mkdir -p \"\$SVC_REMOTE_BASE\" \"\$SVC_CACHE_BASE\"; \
    if [ \"\$DEPLOY_RUNTIME\" = \"1\" ] && [ -f \"\$BOARD_DEB_PATH\" ]; then \
      rm -rf ${BOARD_OUTPUT_BASE}/opt/buddy; \
      dpkg -x \"\$BOARD_DEB_PATH\" ${BOARD_OUTPUT_BASE}; \
      sha256sum \"\$BOARD_DEB_PATH\" | awk '{print \$1}' > \"\$DEB_REMOTE_SHA_PATH\"; \
    fi; \
    if [ ! -d ${BOARD_OUTPUT_BASE}/opt/buddy ]; then \
      echo '[ERROR] runtime not deployed and target ${BOARD_OUTPUT_BASE}/opt/buddy is missing'; \
      exit 1; \
    fi; \
    if [ \"\$DEPLOY_MODELS\" = \"1\" ] && [ -f \"\$MODELS_REMOTE_PATH\" ]; then \
      rm -rf \"\$MODELS_REMOTE_CACHE_DIR\"; \
      mkdir -p \"\$MODELS_REMOTE_CACHE_DIR\"; \
      tar xzf \"\$MODELS_REMOTE_PATH\" -C \"\$MODELS_REMOTE_CACHE_DIR\"; \
      sha256sum \"\$MODELS_REMOTE_PATH\" | awk '{print \$1}' > \"\$MODELS_REMOTE_SHA_PATH\"; \
    fi; \
    if [ -d \"\$MODELS_REMOTE_CACHE_DIR\" ]; then \
      rm -rf ${BOARD_OUTPUT_BASE}/opt/buddy/models; \
      ln -sfn \"\$MODELS_REMOTE_CACHE_DIR\" ${BOARD_OUTPUT_BASE}/opt/buddy/models; \
    fi; \
    if [ \"\$DEPLOY_SVC_LLM\" = \"1\" ] && [ -f \"\$SVC_REMOTE_BASE/buddy-service-llm.tar.gz\" ]; then \
      rm -rf \"\$SVC_CACHE_BASE/llm\"; mkdir -p \"\$SVC_CACHE_BASE/llm\"; \
      tar xzf \"\$SVC_REMOTE_BASE/buddy-service-llm.tar.gz\" -C \"\$SVC_CACHE_BASE/llm\"; \
      sha256sum \"\$SVC_REMOTE_BASE/buddy-service-llm.tar.gz\" | awk '{print \$1}' > \"\$SVC_REMOTE_BASE/buddy-service-llm.sha256\"; \
      rm -f ${BOARD_OUTPUT_BASE}/opt/buddy/scripts/start_llm_server.sh; \
      rm -rf ${BOARD_OUTPUT_BASE}/opt/buddy/services/llm ${BOARD_OUTPUT_BASE}/opt/buddy/rkllm_server; \
      cp -a \"\$SVC_CACHE_BASE/llm/.\" ${BOARD_OUTPUT_BASE}/opt/buddy/; \
    fi; \
    if [ \"\$DEPLOY_SVC_FUNASR\" = \"1\" ] && [ -f \"\$SVC_REMOTE_BASE/buddy-service-funasr.tar.gz\" ]; then \
      rm -rf \"\$SVC_CACHE_BASE/funasr\"; mkdir -p \"\$SVC_CACHE_BASE/funasr\"; \
      tar xzf \"\$SVC_REMOTE_BASE/buddy-service-funasr.tar.gz\" -C \"\$SVC_CACHE_BASE/funasr\"; \
      sha256sum \"\$SVC_REMOTE_BASE/buddy-service-funasr.tar.gz\" | awk '{print \$1}' > \"\$SVC_REMOTE_BASE/buddy-service-funasr.sha256\"; \
      rm -f ${BOARD_OUTPUT_BASE}/opt/buddy/scripts/start_asr_server.sh ${BOARD_OUTPUT_BASE}/opt/buddy/bin/funasr-wss-server ${BOARD_OUTPUT_BASE}/opt/buddy/bin/funasr-wss-server-2pass; \
      rm -rf ${BOARD_OUTPUT_BASE}/opt/buddy/lib/funasr; \
      cp -a \"\$SVC_CACHE_BASE/funasr/.\" ${BOARD_OUTPUT_BASE}/opt/buddy/; \
    fi; \
    if [ \"\$DEPLOY_SVC_CHATTTS\" = \"1\" ] && [ -f \"\$SVC_REMOTE_BASE/buddy-service-chattts.tar.gz\" ]; then \
      rm -rf \"\$SVC_CACHE_BASE/chattts\"; mkdir -p \"\$SVC_CACHE_BASE/chattts\"; \
      tar xzf \"\$SVC_REMOTE_BASE/buddy-service-chattts.tar.gz\" -C \"\$SVC_CACHE_BASE/chattts\"; \
      sha256sum \"\$SVC_REMOTE_BASE/buddy-service-chattts.tar.gz\" | awk '{print \$1}' > \"\$SVC_REMOTE_BASE/buddy-service-chattts.sha256\"; \
      rm -f ${BOARD_OUTPUT_BASE}/opt/buddy/scripts/start_tts_server.sh; \
      rm -rf ${BOARD_OUTPUT_BASE}/opt/buddy/services/tts; \
      cp -a \"\$SVC_CACHE_BASE/chattts/.\" ${BOARD_OUTPUT_BASE}/opt/buddy/; \
    fi; \
    test -x ${BOARD_OUTPUT_BASE}/opt/buddy/run.sh; \
    echo '[OK] deploy ready at ${BOARD_OUTPUT_BASE}/opt/buddy'"
log_ok "Deploy complete"
else
log_stage "Step 3/5: Apply runtime/models/services on board"
log_skip "Deploy action disabled"
fi

if [[ "$DO_RUN" == "1" ]]; then
    log_stage "Step 4/5: Run buddy and capture log"
    log_step "Running on board for ${RUN_SECONDS}s -> /tmp/buddy-run.log"
    set +e
    "${SSH_BASE[@]}" "set -euo pipefail; \
        cd ${BOARD_OUTPUT_BASE}/opt/buddy; \
        rm -f /tmp/buddy-run.log /tmp/buddy-run.status; \
        set +e; timeout --signal=INT ${RUN_SECONDS} ./run.sh > /tmp/buddy-run.log 2>&1; rc=\$?; set -e; \
        if [ \$rc -eq 124 ] || [ \$rc -eq 130 ]; then rc=0; fi; \
        pkill -f '/opt/buddy/bin/buddy_main' >/dev/null 2>&1 || true; \
        pkill -f '/output/opt/buddy/bin/buddy_main' >/dev/null 2>&1 || true; \
        echo \$rc > /tmp/buddy-run.status; \
        tail -n 80 /tmp/buddy-run.log"
    run_ssh_rc=$?
    set -e
    if [[ "$run_ssh_rc" -ne 0 ]]; then
        log_err "Board run command returned non-zero (${run_ssh_rc}); will still fetch logs"
    fi

    if [[ "$DO_FETCH_LOG" == "1" ]]; then
        ts="$(date +%Y%m%d_%H%M%S)"
        local_log="${LOCAL_LOG_DIR}/buddy-run_${BOARD_HOST}_${ts}.log"
        log_step "Fetching /tmp/buddy-run.log -> ${local_log}"
        "${SCP_BASE[@]}" "${BOARD_USER}@${BOARD_HOST}:/tmp/buddy-run.log" "$local_log"
        log_ok "Log fetched: $local_log"
    else
        log_skip "Fetch log"
    fi
else
    log_skip "Run app"
    if [[ "$DO_FETCH_LOG" == "1" ]]; then
        ts="$(date +%Y%m%d_%H%M%S)"
        local_log="${LOCAL_LOG_DIR}/buddy-run_${BOARD_HOST}_${ts}.log"
        log_step "Fetching existing /tmp/buddy-run.log -> ${local_log}"
        set +e
        "${SCP_BASE[@]}" "${BOARD_USER}@${BOARD_HOST}:/tmp/buddy-run.log" "$local_log"
        fetch_rc=$?
        set -e
        if [[ "$fetch_rc" -eq 0 ]]; then
            log_ok "Log fetched: $local_log"
        else
            log_err "No /tmp/buddy-run.log on board (or fetch failed)"
        fi
    fi
fi

log_stage "Step 5/5: Done"
log_ok "Workflow completed"
