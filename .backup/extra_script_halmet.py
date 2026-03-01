# This script adds a build middleware to fix NodeList issues in the halmet environment.
# It ensures that any NodeList returned by previous middleware is flattened to a single node.
# This only occurs with PIOArduino platform

def flatten_nodelist_middleware(env, node):
    try:
        from SCons.Node import NodeList
    except ImportError:
        NodeList = list
    if isinstance(node, (list, NodeList)):
        if node:
            # print("[flatten_nodelist_middleware] NodeList detected, using first element")
            return node[0]
        else:
            # print("[flatten_nodelist_middleware] Empty NodeList")
            return None
    return node

Import("env")
# Remove and re-add to ensure this middleware is last in the chain
if hasattr(env, '_build_middlewares'):
    try:
        env._build_middlewares.remove(flatten_nodelist_middleware)
    except ValueError:
        pass
    env._build_middlewares.append(flatten_nodelist_middleware)
else:
    env.AddBuildMiddleware(flatten_nodelist_middleware)
