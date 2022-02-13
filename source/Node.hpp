#pragma once
// #include "Data.hpp"
#include <unordered_set>

template <typename T>
class Node {
  private:
    T _node;
    std::unordered_set<Node<T>*> _neighborhood;
    // Data<T> _data;
    static std::unordered_set<Node<T>*> network;

  public:
    Node(T p)
        : _node {p}
        , _neighborhood {}
    // , _data {Data<T> {p}}
    {
        this->network.emplace(this);
    }

    T& get()
    {
        return _node;
    }

    // Data<T>& data()
    // {
    //     return _data.get();
    // }

    std::unordered_set<Node<T>*>& neighborhood();
};