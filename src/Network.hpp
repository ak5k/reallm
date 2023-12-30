#include <algorithm>
#include <map>
#include <vector>

template <typename Node>
class Network
{
public:
  void addNode(Node node)
  {
    nodes[node] = std::vector<Node>{};
  }

  void addLink(Node fromNode, Node toNode)
  {
    nodes[fromNode].push_back(toNode);
  }

  void findAllPaths(Node startNode, Node endNode, std::vector<Node> path,
                    std::vector<std::vector<Node>>& allPaths)
  {
    path.push_back(startNode);

    if (startNode == endNode)
    {
      allPaths.push_back(path);
    }
    else
    {
      for (Node node : nodes[startNode])
      {
        if (std::find(path.begin(), path.end(), node) == path.end())
        {
          findAllPaths(node, endNode, path, allPaths);
        }
      }
    }

    path.pop_back();
  }

  std::vector<std::vector<Node>> getAllPaths(Node startNode, Node endNode)
  {
    std::vector<std::vector<Node>> allPaths;
    std::vector<Node> path;
    findAllPaths(startNode, endNode, path, allPaths);
    return allPaths;
  }

  std::map<Node, std::vector<Node>>& getNodes()
  {
    return nodes;
  }

private:
  std::map<Node, std::vector<Node>> nodes{};
};