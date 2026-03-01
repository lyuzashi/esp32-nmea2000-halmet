

# Modify extra_script.py with this function to prevent 
# AttributeError: 'NodeList' object has no attribute 'srcnode':
#  File "/Users/ben/.pyenv/versions/3.12.4/lib/python3.12/site-packages/platformio/builder/tools/piobuild.py", line 330:
#    new_node = callback(new_node)
#  File "/Users/ben/.platformio/platforms/espressif32/builder/frameworks/espidf.py", line 2424:
#    node_path_resolved = Path(node.srcnode().get_path()).resolve()



    # def injectIncludes(env, node):
    #     def node_desc(n):
    #         try:
    #             return f"type={type(n)}, path={n.get_path()}"
    #         except Exception:
    #             return f"type={type(n)}, value={repr(n)}"

    #     print(f"[injectIncludes DEBUG] input {node_desc(node)}")
    #     try:
    #         from SCons.Node import NodeList
    #     except ImportError:
    #         NodeList = list
    #     # Always call env.Object, but flatten result to a single node
    #     result = env.Object(node, CPPPATH=env["CPPPATH"]+GLOBAL_INCLUDES)
    #     if isinstance(result, (list, NodeList)):
    #         if result:
    #             print(f"[injectIncludes DEBUG] env.Object returned NodeList, using first element: {node_desc(result[0])}")
    #             return result[0]
    #         else:
    #             print(f"[injectIncludes DEBUG] env.Object returned empty NodeList")
    #             return None
    #     return result
    # env.AddBuildMiddleware(injectIncludes)