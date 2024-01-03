#include <unordered_map>
#include <unordered_set>

template <typename Node>
class Network
{
public:
  void addNode(Node node)
  {
    nodes[node] = std::unordered_set<Node>{};
  }

  void addLink(Node fromNode, Node toNode)
  {
    nodes[fromNode].insert(toNode);
  }

  std::unordered_map<Node, std::unordered_set<Node>>& getNodes()
  {
    return nodes;
  }

  void clear()
  {
    nodes.clear();
  }

private:
  std::unordered_map<Node, std::unordered_set<Node>> nodes{};
};