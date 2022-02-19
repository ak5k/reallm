#include <algorithm>
#include <vector>

#define VECTORSIZE 8

namespace llm {

template <typename T, typename U, typename V>
class Network { // : public Node<T> {
  public:
    Network(T k, U& u)
        : node {k}
        , routes {}
        , stack {}
        , results {u}
        , res {}
    {
        routes.reserve(VECTORSIZE);
        stack.reserve(VECTORSIZE);
    }

    T& get()
    {
        return node;
    }

    std::vector<T> get_neighborhood(T& k);

    U get_results()
    {
        return results;
    }

    void set_results(U& r)
    {
        results = std::move(r);
        return;
    }

    std::vector<std::vector<T>>& get_routes()
    {
        return routes;
    }

    void traverse(bool do_analyze)
    {
        traverse(node, results, res, do_analyze);
        return;
    }

  private:
    T node;
    std::vector<std::vector<T>> routes;
    std::vector<T> stack;
    U& results;
    V res;

    V& analyze(T& k, U& r, V& v);

    void traverse(T& k, U& r, V& v, bool& do_analyze)
    {
        if (do_analyze == true) {
            v = analyze(k, r, v); // accumulates results
        }
        auto neighborhood = get_neighborhood(k);

        if (neighborhood.empty()) {
            stack.push_back(k);
            routes.emplace_back(stack);
            stack.pop_back();
            return;
        }
        else {
            for (auto&& i : neighborhood) {
                if (find(stack.begin(), stack.end(), i) == stack.end()) {
                    stack.push_back(k);
                    traverse(i, r, v, do_analyze);
                    stack.pop_back();
                }
            }
            return;
        }
    }
};

} // namespace llm