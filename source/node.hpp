#include <algorithm>
#include <vector>

namespace llm {

template <typename T, typename U, typename V>
class Node {
  public:
    Node(T k)
        : _node {k}
        , _neighborhood {}
        , stack {}
        , routes {}
        , results {}
        , res {}
    {
    }

    T& get()
    {
        return _node;
    }

    std::vector<U>& traverse(bool do_analyze)
    {
        return traverse(_node, res, do_analyze);
    }

    std::vector<T>& neighborhood();

  private:
    T _node;
    std::vector<T> _neighborhood;
    std::vector<T> stack;
    std::vector<std::vector<T>> routes;
    std::vector<U> results;
    V res;

    V& analyze(T& k, V& v);

    std::vector<U>& traverse(T& k, V& v, bool& do_analyze)
    {
        if (do_analyze == true) {
            v = analyze(k, v); // accumulates results
        }

        if (this->neighborhood().empty()) {
            stack.push_back(k);
            routes.emplace_back(stack);
            stack.pop_back();
            return results;
        }
        else {
            for (auto i : this->neighborhood()) {
                if (find(stack.begin(), stack.end(), i) == stack.end()) {
                    stack.push_back(i);
                    results = traverse(i, v, do_analyze);
                    stack.pop_back();
                }
            }
            return results;
        }
    }
};

} // namespace llm