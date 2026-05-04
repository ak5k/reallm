#include "network.h"

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

TEST(NetworkTest, AddNodeCreatesEntry)
{
    Network<int> net;
    net.addNode(1);
    EXPECT_EQ(net.getNodes().size(), 1);
}

TEST(NetworkTest, AddLinkConnectsNodes)
{
    Network<int> net;
    net.addNode(1);
    net.addNode(2);
    net.addLink(1, 2);
    EXPECT_EQ(net.getNodes()[1].count(2), 1);
    EXPECT_EQ(net.getNodes()[2].count(1), 0);
}

TEST(NetworkTest, ClearEmptiesNetwork)
{
    Network<int> net;
    net.addNode(1);
    net.addLink(1, 1);
    net.clear();
    EXPECT_TRUE(net.getNodes().empty());
}

namespace
{

void dfs(Network<int>& network, int current, int target, std::vector<int>& visited,
         std::vector<int>& path, std::vector<std::vector<int>>& allPaths)
{
    visited.push_back(current);
    path.push_back(current);

    if (current == target)
    {
        allPaths.push_back(path);
    }
    else
    {
        for (auto&& next : network.getNodes()[current])
            if (std::find(visited.begin(), visited.end(), next) == visited.end())
                dfs(network, next, target, visited, path, allPaths);
    }

    path.pop_back();
    visited.pop_back();
}

std::vector<std::vector<int>> findAllPaths(Network<int>& net, int from, int to)
{
    std::vector<std::vector<int>> result;
    std::vector<int> visited;
    std::vector<int> path;
    dfs(net, from, to, visited, path, result);
    return result;
}

} // namespace

TEST(NetworkTraversalTest, SplitAndMergeReturnsAllPaths)
{
    // A -> B -> D -> E
    // A -> C -> B -> D -> E  (split at A, merge at B)
    Network<int> net;
    for (int n : {1, 2, 3, 4, 5})
        net.addNode(n);
    net.addLink(1, 2);
    net.addLink(1, 3);
    net.addLink(2, 4);
    net.addLink(3, 2);
    net.addLink(4, 5);

    auto paths = findAllPaths(net, 1, 5);

    std::vector<std::vector<int>> expected{{1, 2, 4, 5}, {1, 3, 2, 4, 5}};

    std::sort(paths.begin(), paths.end());
    std::sort(expected.begin(), expected.end());

    EXPECT_EQ(paths, expected);
}
