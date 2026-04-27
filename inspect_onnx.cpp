#include <onnxruntime/onnxruntime_cxx_api.h>
#include <iostream>
#include <vector>
#include <string>

int main() {
    const char* model_path = "android/app/src/main/assets/models/model.onnx";
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "ONNX_Inspection");
    
    try {
        Ort::SessionOptions session_options;
        // Just load the model, don't necessarily optimize it to avoid errors seen in python
        session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
        
        Ort::Session session(env, model_path, session_options);
        Ort::AllocatorWithDefaultOptions allocator;

        std::cout << "--- ONNX Metadata Inspection (C++ API) ---" << std::endl;

        std::cout << "\nINPUTS:" << std::endl;
        size_t num_input_nodes = session.GetInputCount();
        for (size_t i = 0; i < num_input_nodes; i++) {
            auto name = session.GetInputNameAllocated(i, allocator);
            auto type_info = session.GetInputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto type = tensor_info.GetElementType();
            auto shape = tensor_info.GetShape();

            std::cout << "Name: " << name.get() << " | Shape: [";
            for (size_t j = 0; j < shape.size(); j++) {
                std::cout << (shape[j] < 0 ? "dynamic" : std::to_string(shape[j])) << (j < shape.size() - 1 ? ", " : "");
            }
            std::cout << "] | Type: " << (int)type << std::endl;
        }

        std::cout << "\nOUTPUTS:" << std::endl;
        size_t num_output_nodes = session.GetOutputCount();
        for (size_t i = 0; i < num_output_nodes; i++) {
            auto name = session.GetOutputNameAllocated(i, allocator);
            auto type_info = session.GetOutputTypeInfo(i);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto type = tensor_info.GetElementType();
            auto shape = tensor_info.GetShape();

            std::cout << "Name: " << name.get() << " | Shape: [";
            for (size_t j = 0; j < shape.size(); j++) {
                std::cout << (shape[j] < 0 ? "dynamic" : std::to_string(shape[j])) << (j < shape.size() - 1 ? ", " : "");
            }
            std::cout << "] | Type: " << (int)type << std::endl;
        }

    } catch (const Ort::Exception& e) {
        std::cerr << "ORT Exception: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Standard Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
