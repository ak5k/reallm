#pragma once
#include <set>

template <typename T>
class Data {
  private:
    T data;
    std::set<T> elements;

  public:
    Data(T p)
        : data {p}
    {
    }

    Data<T>& get()
    {
        return *this;
    }
};