#include "neural_network.hpp"
#include <utils/sigmoid.hpp>
#include <algorithm>

neural_network::neural_network(std::vector<uint32_t> sizes)
  : rand_gen_(std::random_device{}())
  , sizes_(std::move(sizes)) {
  std::normal_distribution<> randn_biase;
  biases_.reserve(sizes_.size() - 1);
  for (auto i = 1; i < sizes_.size(); ++i) {
    biases_.emplace_back(vector::Zero(sizes_[i], 1).unaryExpr(
      [&](double x) {
        return x + randn_biase(rand_gen_);
      }
    ));
  }
  weights_.reserve(sizes_.size() - 1);
  for (auto i = 0; i < sizes_.size() - 1; ++i) {
    std::normal_distribution<> randn_weight(0.0, 1.0 / sqrt(sizes_[i]));
    weights_.emplace_back(matrix::Zero(sizes_[i + 1], sizes_[i]).unaryExpr(
      [&](double x) {
        return x + randn_weight(rand_gen_);
      }
    ));
  }
  velocities_.reserve(sizes_.size() - 1);
}

vector neural_network::feedforward(vector a) const {
  for (auto i = 0; i < sizes_.size() - 1; ++i) {
    a = (weights_[i] * a + biases_[i]).unaryExpr(std::ptr_fun(sigmoid));
  }
  return a;
}

void neural_network::sgd_train(data_set& training_data, uint32_t epochs,
                               uint32_t mini_batch_size, double eta,
                               double lambda, double mu,
                               evaluator f/* = evaluator() */) {
  velocities_.clear();
  for (auto i = 0; i < sizes_.size() - 1; ++i) {
    velocities_.emplace_back(matrix::Zero(sizes_[i + 1], sizes_[i]));
  }
  for (auto i = 0; i < epochs; ++i) {
    std::shuffle(training_data.begin(), training_data.end(), rand_gen_);
    for (auto j = 0; j < training_data.size(); j += mini_batch_size) {
      auto it0 = training_data.begin() + j;
      auto it1 =
        j + mini_batch_size < training_data.size() ? it0 + mini_batch_size
                                                   : training_data.end();
      update_mini_batch(it0, it1, training_data.size(), eta, lambda, mu);
    }
    if (f && f(*this, i)) {
      break;
    }
  }
}

void neural_network::update_mini_batch(data_set::const_iterator it0,
                                       data_set::const_iterator it1,
                                       size_t total_size, double eta,
                                       double lambda, double mu) {
  auto batch_size = it1 - it0;
  matrix activation(sizes_.front(), batch_size);
  matrix label(sizes_.back(), batch_size);
  for (auto i = 0; i < batch_size; ++i) {
    activation.col(i) = (it0 + i)->first;
    label.col(i) = (it0 + i)->second;
  }
  auto nabla = backprop(activation, label);
  for (auto i = 0; i < biases_.size(); ++i) {
    biases_[i] -= eta / batch_size * nabla.first[i];
  }
  for (auto i = 0; i < weights_.size(); ++i) {
    velocities_[i] *= mu;
    velocities_[i] -= eta / batch_size * nabla.second[i];
    weights_[i] *= 1.0 - eta * lambda / total_size;
    weights_[i] += velocities_[i];
  }
}

std::pair<std::vector<vector>, std::vector<matrix>>
neural_network::backprop(matrix activation, const matrix& label) {
  std::vector<matrix> as;
  as.reserve(sizes_.size());
  as.emplace_back(activation);
  std::vector<matrix> dzs;
  dzs.reserve(sizes_.size() - 1);
  for (auto i = 0; i < sizes_.size() - 1; ++i) {
    activation = ((weights_[i] * activation).colwise() + biases_[i])
                   .unaryExpr(std::ptr_fun(sigmoid));
    as.emplace_back(activation);
    dzs.emplace_back(activation.unaryExpr(std::ptr_fun(dsigmoid)));
  }
  matrix delta = as.back() - label;
  std::vector<vector> nabla_b;
  nabla_b.reserve(biases_.size());
  for (auto& b: biases_) {
    nabla_b.emplace_back(vector::Zero(b.rows(), b.cols()));
  }
  nabla_b.back() = delta.rowwise().sum();
  std::vector<matrix> nabla_w;
  nabla_w.reserve(weights_.size());
  for (auto& w: weights_) {
    nabla_w.emplace_back(matrix::Zero(w.rows(), w.cols()));
  }
  nabla_w.back() = delta * (as.end() - 2)->transpose();
  for (auto i = 2; i < sizes_.size(); ++i) {
    delta = ((weights_.end() - i + 1)->transpose() * delta)
              .cwiseProduct(*(dzs.end() - i));
    *(nabla_b.end() - i) = delta.rowwise().sum();
    *(nabla_w.end() - i) = delta * (as.end() - i - 1)->transpose();
  }
  return {std::move(nabla_b), std::move(nabla_w)};
}
