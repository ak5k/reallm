#include "network.h"

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
