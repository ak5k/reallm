#include <algorithm>
#include <unordered_map>
#include <vector>

namespace llm {

template <typename T, typename U, typename V>
class Node {
  public:
    Node(T k)
        : node {k}
        , stack {}
        , routes {}
        , results {}
        , res {}
    {
    }

    std::vector<T> get_neighborhood(T& k);

    std::vector<std::vector<T>>& get_routes()
    {
        return routes;
    }

    std::vector<U>& traverse(bool do_analyze)
    {
        return traverse(node, res, do_analyze);
    }

  private:
    T node;
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
        auto neighborhood = get_neighborhood(k);

        if (neighborhood.empty()) {
            stack.push_back(k);
            routes.emplace_back(stack);
            stack.pop_back();
            return results;
        }
        else {
            for (auto i : neighborhood) {
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