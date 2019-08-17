# Almost a direct port of entt. It works, and well. Also happens to be 
# nicely self-contained

ADD_MODULE(ecs_module
    "include/ComponentMap.hpp"
    "include/DynamicView.hpp"
    "include/Entity.hpp"
    "include/EntitySparseSet.hpp"
    "include/Registry.hpp"
    "include/ViewTypes.hpp"
    "src/ComponentMap.cpp"
    "src/DynamicView.cpp"
    "src/EntitySparseSet.cpp"
    "src/Registry.cpp"
    "src/ViewTypes.cpp"
)

TARGET_LINK_LIBRARIES(ecs_module PUBLIC easyloggingpp)
