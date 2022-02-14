#include <algorithm>
#include <unordered_set>
#include <vector>

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

    void traverse(bool do_analyze)
    {
        traverse(_node, res, do_analyze);
    }

    std::unordered_set<T>& neighborhood();

  private:
    T _node;
    std::unordered_set<T> _neighborhood;
    std::vector<T> stack;
    std::vector<std::vector<T>> routes;
    std::unordered_set<U> results;
    V res;

    V& analyze(T& k, V& v);

    void traverse(T& k, V& v, bool& do_analyze)
    {
        if (do_analyze == true) {
            v = analyze(k, v);
        }

        if (this->neighborhood().empty()) {
            stack.push_back(k);
            routes.emplace_back(stack);
            stack.pop_back();
            return;
        }
        else {
            for (auto i : this->neighborhood()) {
                if (find(stack.begin(), stack.end(), i) == stack.end()) {
                    stack.push_back(i);
                    traverse(i, v, do_analyze);
                    stack.pop_back();
                }
            }
            return;
        }
    }
};