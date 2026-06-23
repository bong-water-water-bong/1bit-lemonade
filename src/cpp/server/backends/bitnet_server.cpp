#include "lemon/backends/bitnet_server.h"
#include "lemon/backends/backend_utils.h"
#include "lemon/model_manager.h"
#include "lemon/runtime_config.h"
#include "lemon/system_info.h"
#include "lemon/utils/http_client.h"
#include "lemon/utils/process_manager.h"
#include <lemon/utils/aixlog.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace lemon::utils;

namespace lemon {
namespace backends {

InstallParams BitNetServer::get_install_params(const std::string& backend, const std::string& version) {
    InstallParams params;

    if (backend == "rocm") {
        // rocm-cpp is built from source via TheRock ROCm.
        // bitnet_decode must be on PATH or at a known location.
        // For now, use builtin — the binary is expected to be pre-installed.
        // Future: download prebuilt binaries from bong-water-water-bong/rocm-cpp releases.
        params.repo = "bong-water-water-bong/rocm-cpp";
        params.version_override = version;
        std::string target_arch = SystemInfo::get_rocm_arch();
        if (target_arch.empty()) {
            throw std::runtime_error(
                SystemInfo::get_unsupported_backend_error("bitnet", "rocm")
            );
        }
#ifdef __linux__
        // One release per GPU target: tag = {version}-{arch}
        std::string release_tag = version + "-" + target_arch;
        params.version_override = release_tag;
        params.filename = release_tag + "-x64.tar.gz";
#else
        throw std::runtime_error("rocm-cpp (bitnet) is only supported on Linux");
#endif
    } else {
        throw std::runtime_error("BitNet backend '" + backend + "' is not supported. Supported: rocm");
    }

    return params;
}

BitNetServer::BitNetServer(const std::string& log_level, ModelManager* model_manager, BackendManager* backend_manager)
    : WrappedServer("bitnet_decode", log_level, model_manager, backend_manager) {
}

BitNetServer::~BitNetServer() {
    unload();
}

void BitNetServer::load(const std::string& model_name,
                        const ModelInfo& model_info,
                        const RecipeOptions& options,
                        bool do_not_upgrade) {
    LOG(INFO, "BitNet") << "Loading 1-bit model: " << model_name << std::endl;

    std::string bitnet_backend = options.get_option("bitnet_backend");
    std::string bitnet_args = options.get_option("bitnet_args");

    RuntimeConfig::validate_backend_choice("bitnet", bitnet_backend);

    // The checkpoint is the path to the .h1b model file
    std::string model_path = model_info.checkpoint();
    if (model_path.empty()) {
        model_path = model_info.resolved_path();
    }
    if (model_path.empty()) {
        throw std::runtime_error("Model checkpoint (.h1b path) not found for: " + model_name);
    }

    if (!fs::exists(model_path)) {
        throw std::runtime_error("BitNet model file not found: " + model_path);
    }

    LOG(DEBUG, "BitNet") << "Using model: " << model_path << std::endl;

    // Choose port
    port_ = choose_port();

    // Find bitnet_decode binary
    std::string executable;
    // First try BackendUtils for a managed install
    try {
        executable = BackendUtils::get_backend_binary_path(SPEC, bitnet_backend);
    } catch (...) {
        // Fall back to PATH lookup
        executable = "bitnet_decode";
    }

    // Build command line
    std::vector<std::string> args;
    args.push_back("--server");
    args.push_back("--port");
    args.push_back(std::to_string(port_));
    args.push_back("--bind");
    args.push_back("127.0.0.1");
    args.push_back("--model");
    args.push_back(model_path);
    // bitnet_decode serves the model name as-is; pass it through
    args.push_back("--served-model-name");
    args.push_back(model_name);

    // Append custom bitnet_args if provided
    if (!bitnet_args.empty()) {
        LOG(DEBUG, "BitNet") << "Adding custom arguments: " << bitnet_args << std::endl;
        std::istringstream iss(bitnet_args);
        std::string arg;
        while (iss >> arg) {
            args.push_back(arg);
        }
    }

    LOG(INFO, "BitNet") << "Starting bitnet_decode on port " << port_ << "..." << std::endl;

    // Set environment for ROCm
    std::vector<std::pair<std::string, std::string>> env_vars;
    // These are the standard rocm-cpp runtime requirements (from rocm-cpp README)
    env_vars.push_back({"HSA_OVERRIDE_GFX_VERSION", "11.5.1"});
    env_vars.push_back({"HSA_ENABLE_SDMA", "0"});
    // Preserve existing LD_LIBRARY_PATH for ROCm libs
    const char* existing_ld = std::getenv("LD_LIBRARY_PATH");
    if (existing_ld) {
        env_vars.push_back({"LD_LIBRARY_PATH", existing_ld});
    }

    // Start process
    bool inherit_output = (log_level_ == "info") || is_debug();
    process_handle_ = ProcessManager::start_process(executable, args, "", inherit_output, true, env_vars);

    // bitnet_decode loads models quickly (no JIT compilation)
    if (!wait_for_ready("/health", 120)) {
        ProcessManager::stop_process(process_handle_);
        process_handle_ = {nullptr, 0};
        throw std::runtime_error("bitnet_decode failed to start within timeout");
    }

    LOG(INFO, "BitNet") << "1-bit model loaded on port " << port_ << std::endl;
}

void BitNetServer::unload() {
    LOG(INFO, "BitNet") << "Unloading 1-bit model..." << std::endl;
#ifdef _WIN32
    if (process_handle_.handle) {
#else
    if (process_handle_.pid > 0) {
#endif
        ProcessManager::stop_process(process_handle_);
        process_handle_ = {nullptr, 0};
        port_ = 0;
    }
}

json BitNetServer::chat_completion(const json& request) {
    return forward_request("/v1/chat/completions", request);
}

json BitNetServer::completion(const json& request) {
    return forward_request("/v1/completions", request);
}

json BitNetServer::responses(const json& request) {
    return forward_request("/v1/responses", request);
}

} // namespace backends
} // namespace lemon
