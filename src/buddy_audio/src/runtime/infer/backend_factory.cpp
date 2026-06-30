#include "buddy_audio/runtime/infer/backend.hpp"

#include <stdexcept>

namespace infer {

// Forward declarations
std::unique_ptr<InferBackend> create_ort_backend();
#ifdef HAS_RKNN
std::unique_ptr<InferBackend> create_rknn_backend();
#endif

std::unique_ptr<InferBackend> create_infer_backend(const std::string& runtime) {
#ifdef HAS_RKNN
    if (runtime == "rknnruntime") {
        return create_rknn_backend();
    }
#endif
    if (runtime == "onnxruntime") {
        return create_ort_backend();
    }
    throw std::runtime_error("Unknown infer runtime: " + runtime);
}

}  // namespace infer
