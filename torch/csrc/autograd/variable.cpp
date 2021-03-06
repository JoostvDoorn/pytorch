#include "torch/csrc/autograd/variable.h"

#ifdef WITH_CUDA
#include "torch/csrc/cuda/AutoGPU.h"
#endif

using namespace torch;
using namespace thpp;

namespace torch { namespace autograd {

Variable::Variable(
  std::unique_ptr<thpp::Tensor> data,
  bool requires_grad,
  bool is_volatile)
    : data(std::move(data))
    , creator(nullptr)
    , grad(nullptr)
    , version_counter(new VariableVersion())
    , output_nr(0)
    , backward_hook()
    , pyobj(nullptr)
{
  if (!this->data) {
    throw std::runtime_error("Variable data is NULL");
  }
  this->is_volatile = is_volatile;
  this->requires_grad = requires_grad;
}

Variable::Variable(
  std::unique_ptr<thpp::Tensor> data,
  std::shared_ptr<Function> creator)
    : data(std::move(data))
    , creator(creator)
    , grad(nullptr)
    , version_counter(new VariableVersion())
    , output_nr(creator->num_outputs++)
    , backward_hook()
    , pyobj(nullptr)
{
  if (!this->data) {
    throw std::runtime_error("Variable data is NULL");
  }
  this->is_volatile = creator->is_volatile;
  this->requires_grad = creator->requires_grad;
  previous_functions.resize(1);
  previous_functions[0] = std::make_pair<>(creator, output_nr);
}

bool Variable::is_cuda()
{
  return data->isCuda();
}

auto Variable::backward(std::shared_ptr<Variable> gradOutput) -> void {
  if (backward_hook) {
    gradOutput = (*backward_hook)(gradOutput);
  }
#ifdef WITH_CUDA
  THCPAutoGPU auto_gpu(gradOutput->data->getDevice());
#endif
  if (!grad) {
    std::unique_ptr<Tensor> data(gradOutput->data->clone());
    grad = std::make_shared<Variable>(std::move(data), false, true);
  } else {
    grad->data->cadd(*grad->data, *gradOutput->data);
  }
}

auto Variable::apply(const variable_list& gradOutputs) -> variable_list {
  if (creator || **version_counter != 0) {
    throw std::runtime_error("leaf variable was used in an inplace operation");
  }
  if (gradOutputs.size() != 1) {
    throw std::runtime_error("incorrect number of gradOutputs");
  }
  backward(gradOutputs[0]);
  return variable_list();
}

auto Variable::save() const -> SavedVariable {
  return SavedVariable(
    std::unique_ptr<Tensor>(data->clone_shallow()),
    **version_counter,
    std::unique_ptr<VariableVersion>(version_counter->new_saved_ref()));
}

auto Variable::save_opt(Variable* var) -> SavedVariable {
 return var ? var->save() : SavedVariable();
}

auto SavedVariable::unpack() -> std::unique_ptr<thpp::Tensor>& {
  if (data) {
    int current_version = **version;
    if (expected_version != current_version) {
      throw std::runtime_error("one of the variables "
          "needed for gradient computation has been modified by an "
          "inplace operation");
    }
  }
  return data;
}

}} // namespace torch::autograd
