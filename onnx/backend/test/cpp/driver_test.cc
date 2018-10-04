#include "gtest_utils.h"

#include "onnx/checker.h"
#include "onnx/onnxifi.h"
#include "onnx/onnxifi_loader.h"
#include "onnx/string_utils.h"

/**
 *	In order to not test the backend itself
 *	but only to test the funcionality of this test driver,
 *	include IO and integrity checkers,
 *	please set ONNXIFI_DUMMY_BACKEND to be true when compiling.
 *	By default it is false.
 */
#ifndef ONNXIFI_DUMMY_BACKEND
#define ONNXIFI_DUMMY_BACKEND false
#endif

#ifndef ONNXIFI_TESTDATA_EPS
#define ONNXIFI_TESTDATA_EPS 1e-5
#endif

const float onnxifi_testdata_eps = ONNXIFI_TESTDATA_EPS;

template <typename T>
class CompareOnnxifiData {
 public:
  bool IsEqual(void* P, void* Q) {
    T x = *((T*)P), y = *((T*)Q);
    return ((x - y) > -onnxifi_testdata_eps) &&
        ((x - y) < onnxifi_testdata_eps);
  }
};
class ONNXCppDriverTest
    : public testing::TestWithParam<ONNX_NAMESPACE::testing::ResolvedTestCase> {
 protected:
  std::vector<ONNX_NAMESPACE::testing::ResolvedTestData> protos_;
  ONNX_NAMESPACE::ModelProto model_;
  void SetUp() override {
    ONNX_NAMESPACE::testing::ResolvedTestCase t = GetParam();
    protos_ = t.proto_test_data_;
    model_ = t.model_;
  }

  uint64_t GetDescriptorSize(const onnxTensorDescriptorV1* t) {
    uint64_t d_size = 1;
    for (int i = 0; i < t->dimensions; i++) {
      d_size *= t->shape[i];
    }
    return d_size;
  }

  void GtestAssertMemorySafeSuccess(
      onnxStatus x,
      onnxGraph* graph_pointer,
      onnxifi_library& lib) {
    if (x != ONNXIFI_STATUS_SUCCESS) {
      if (graph_pointer != NULL) {
        lib.onnxReleaseGraph(*graph_pointer);
      }
    }
    ASSERT_EQ(x, ONNXIFI_STATUS_SUCCESS);
  }

  bool IsUnsupported(onnxStatus s, std::string& msg) {
    if (s == ONNXIFI_STATUS_UNSUPPORTED_VERSION) {
      msg = "Unsupported version of ONNX or operator.";
      return true;
    }

    if (s == ONNXIFI_STATUS_UNSUPPORTED_OPERATOR) {
      msg = "Unsupported operator.";
      return true;
    }

    if (s == ONNXIFI_STATUS_UNSUPPORTED_ATTRIBUTE) {
      msg = "Unsupported attribute.";
      return true;
    }

    if (s == ONNXIFI_STATUS_UNSUPPORTED_SHAPE) {
      msg = "Unsupported shape.";
      return true;
    }
    if (s == ONNXIFI_STATUS_UNSUPPORTED_DATATYPE) {
      msg = "Unsupported datatype";
      return true;
    }
    return false;
  }
  bool IsDescriptorEqual(
      const onnxTensorDescriptorV1& x,
      const onnxTensorDescriptorV1& y) {
    if (x.dataType != y.dataType || x.dimensions != y.dimensions) {
      return false;
    }
    const int dims = x.dimensions;
    for (int i = 0; i < dims; i++) {
      if (x.shape[i] != y.shape[i]) {
        return false;
      }
    }
    const uint64_t d_size = GetDescriptorSize(&x);
    void* p1 = (void*)x.buffer;
    void* p2 = (void*)y.buffer;
    bool is_equal = true;
    for (uint64_t i = 0; i < d_size; i++) {
      int offset = 1;
      switch (x.dataType) {
        case ONNXIFI_DATATYPE_UNDEFINED:
        case ONNXIFI_DATATYPE_INT8:
          CompareOnnxifiData<char> compare_int8;
          is_equal &= compare_int8.IsEqual(p1, p2);
          offset = sizeof(char);
          break;
        case ONNXIFI_DATATYPE_UINT8:
          CompareOnnxifiData<unsigned char> compare_uint8;
          is_equal &= compare_uint8.IsEqual(p1, p2);
          offset = sizeof(unsigned char);
          break;
        case ONNXIFI_DATATYPE_FLOAT16:
          // no support now
          break;
        case ONNXIFI_DATATYPE_INT16:
          CompareOnnxifiData<short> compare_int16;
          is_equal &= compare_int16.IsEqual(p1, p2);
          offset = sizeof(short);
          break;
        case ONNXIFI_DATATYPE_UINT16:
          CompareOnnxifiData<unsigned short> compare_uint16;
          is_equal &= compare_uint16.IsEqual(p1, p2);
          offset = sizeof(unsigned short);
          break;
        case ONNXIFI_DATATYPE_FLOAT32:
          CompareOnnxifiData<float> compare_float32;
          is_equal &= compare_float32.IsEqual(p1, p2);
          offset = sizeof(float);
          break;
        case ONNXIFI_DATATYPE_INT32:
          CompareOnnxifiData<int> compare_int32;
          is_equal &= compare_int32.IsEqual(p1, p2);
          offset = sizeof(int);
          break;
        case ONNXIFI_DATATYPE_UINT32:
          CompareOnnxifiData<unsigned int> compare_uint32;
          is_equal &= compare_uint32.IsEqual(p1, p2);
          offset = sizeof(unsigned int);
          break;
        case ONNXIFI_DATATYPE_FLOAT64:
          CompareOnnxifiData<long double> compare_float64;
          is_equal &= compare_float64.IsEqual(p1, p2);
          offset = sizeof(long double);
          break;
        case ONNXIFI_DATATYPE_INT64:
          CompareOnnxifiData<long long> compare_int64;
          is_equal &= compare_int64.IsEqual(p1, p2);
          offset = sizeof(long long);
          break;
        case ONNXIFI_DATATYPE_UINT64:
          CompareOnnxifiData<unsigned long long> compare_uint64;
          is_equal &= compare_uint64.IsEqual(p1, p2);
          offset = sizeof(unsigned long long);
          break;
        case ONNXIFI_DATATYPE_COMPLEX64:
        case ONNXIFI_DATATYPE_COMPLEX128:
          // no support now
          break;
      }
      p1 = (char*)p1 + offset;
      p2 = (char*)p2 + offset;
      if (!is_equal) {
        return false;
      }
    }
    return true;
  }
  void RunAndVerify(
      onnxifi_library& lib,
      onnxBackend& backend,
      onnxBackendID backendID) {
    // Check Model
    ONNX_NAMESPACE::checker::check_model(model_);
    // Check Input&Output Tensors
    ONNX_NAMESPACE::checker::CheckerContext ctx;
    for (auto proto_test_data : protos_) {
      for (auto input : proto_test_data.inputs_) {
        ONNX_NAMESPACE::checker::check_tensor(input, ctx);
      }
      for (auto output : proto_test_data.outputs_) {
        ONNX_NAMESPACE::checker::check_tensor(output, ctx);
      }
    }
    if (!ONNXIFI_DUMMY_BACKEND) {
      onnxGraph graph;
      uint32_t weightCount = model_.graph().initializer_size();
      onnxTensorDescriptorV1 weightDescriptors,
          *weightDescriptors_pointer = NULL;
      if (weightCount != 0) {
        weightDescriptors =
            ONNX_NAMESPACE::testing::ProtoToOnnxTensorDescriptor(
                model_.graph().initializer(0));
        weightDescriptors_pointer = &weightDescriptors;
      }
      std::string serialized_model;
      model_.SerializeToString(&serialized_model);
      std::cout << model_.graph().name() << std::endl;
      auto is_compatible = lib.onnxGetBackendCompatibility(
          backendID, serialized_model.size(), serialized_model.data());
      std::string error_msg;
      if (IsUnsupported(is_compatible, error_msg)) {
        std::cout << "Warnning: " << error_msg
                  << " This test case will be skipped." << std::endl;
        GTEST_SKIP();
        return;
      }
      GtestAssertMemorySafeSuccess(is_compatible, NULL, lib);
      GtestAssertMemorySafeSuccess(
          lib.onnxInitGraph(
              backend,
              NULL,
              serialized_model.size(),
              serialized_model.data(),
              weightCount,
              weightDescriptors_pointer,
              &graph),
          NULL,
          lib);
      for (const auto& proto_test_data : protos_) {
        std::vector<onnxTensorDescriptorV1> input_descriptor, output_descriptor,
            result_descriptor;
        for (const auto& input : proto_test_data.inputs_) {
          input_descriptor.push_back(
              ONNX_NAMESPACE::testing::ProtoToOnnxTensorDescriptor(input));
        }
        int output_count = 0;
        for (auto& output : proto_test_data.outputs_) {
          output_count++;
          output_descriptor.push_back(
              ONNX_NAMESPACE::testing::ProtoToOnnxTensorDescriptor(output));
          onnxTensorDescriptorV1 result;
          result.tag = ONNXIFI_TAG_TENSOR_DESCRIPTOR_V1;
          std::string name_string = output_descriptor[output_count - 1].name;
          result.name = name_string.c_str();
          result.dataType = output.data_type();
          result.memoryType = ONNXIFI_MEMORY_TYPE_CPU;
          std::vector<uint64_t> shape_values(
              output.dims().begin(), output.dims().end());
          result.dimensions = shape_values.size();
          result.shape = shape_values.data();
          std::vector<uint8_t> raw_data(output.raw_data().size(), 0);
          result.buffer = (onnxPointer)raw_data.data();
          result_descriptor.emplace_back(std::move(result));
        }
        GtestAssertMemorySafeSuccess(
            lib.onnxSetGraphIO(
                graph,
                input_descriptor.size(),
                input_descriptor.data(),
                result_descriptor.size(),
                result_descriptor.data()),
            &graph,
            lib);

        onnxMemoryFenceV1 inputFence, outputFence;
        inputFence.tag = ONNXIFI_TAG_MEMORY_FENCE_V1;
        outputFence.tag = ONNXIFI_TAG_MEMORY_FENCE_V1;
        inputFence.type = ONNXIFI_SYNCHRONIZATION_EVENT;
        outputFence.type = ONNXIFI_SYNCHRONIZATION_EVENT;
        GtestAssertMemorySafeSuccess(
            lib.onnxInitEvent(backend, &inputFence.event), &graph, lib);
        GtestAssertMemorySafeSuccess(
            lib.onnxInitEvent(backend, &outputFence.event), &graph, lib);
        GtestAssertMemorySafeSuccess(
            lib.onnxSignalEvent(inputFence.event), &graph, lib);
        GtestAssertMemorySafeSuccess(
            lib.onnxRunGraph(graph, &inputFence, &outputFence), &graph, lib);
        GtestAssertMemorySafeSuccess(
            lib.onnxWaitEvent(outputFence.event), &graph, lib);
        for (int i = 0; i < output_descriptor.size(); i++) {
          auto output_size = GetDescriptorSize(&output_descriptor[i]);
          for (int j = 0; j < output_size; j++) {
            EXPECT_EQ(
                IsDescriptorEqual(output_descriptor[i], result_descriptor[i]),
                true);
          }
        }
      }
      GtestAssertMemorySafeSuccess(lib.onnxReleaseGraph(graph), NULL, lib);
    }
  }
};

/**
 *	When testing backend, we do not specify target backend here,
 *	but always use the by-default backend of onnxifi_loader.
 *	Check onnxifi_loader.c for more detail of how to indicate specific
 *  backend.
 */
TEST_P(ONNXCppDriverTest, ONNXCppDriverUnitTest){
  onnxifi_library lib;
  onnxBackendID* backendIDs = NULL;
  onnxBackend backend;
  const size_t max_info_size = 50; // The maximum backend info size
  if (!ONNXIFI_DUMMY_BACKEND) {
    ASSERT_TRUE(onnxifi_load(1, NULL, &lib));
    size_t numBackends = 0;
    lib.onnxGetBackendIDs(backendIDs, &numBackends);
    backendIDs = (void**)malloc(numBackends * sizeof(onnxBackendID));
    lib.onnxGetBackendIDs(backendIDs, &numBackends);

    for (int i = 0; i < numBackends; i++) {
      const uint64_t backendProperties[] = {ONNXIFI_BACKEND_PROPERTY_NONE};
      lib.onnxInitBackend(backendIDs[i], backendProperties, &backend);
      char infovalue[max_info_size];
      size_t infoValueSize = max_info_size;
      EXPECT_EQ(
          lib.onnxGetBackendInfo(
              backendIDs[i], ONNXIFI_BACKEND_NAME, infovalue, &infoValueSize),
          ONNXIFI_STATUS_SUCCESS);
      RunAndVerify(lib, backend, backendIDs[i]);
      lib.onnxReleaseBackend(backend);
      lib.onnxReleaseBackendID(backendIDs[i]);
    }
    free(backendIDs);
  } else {
    RunAndVerify(lib, backend, NULL);
  }
}
INSTANTIATE_TEST_CASE_P(
    ONNXCppAllTest,
    ONNXCppDriverTest,
    testing::ValuesIn(GetTestCases()));