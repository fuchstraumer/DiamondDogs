#pragma once
#ifndef DIAMOND_DOGS_BOOTSTRAP_CONTENT_STAGE_HPP
#define DIAMOND_DOGS_BOOTSTRAP_CONTENT_STAGE_HPP
#include <span>
#include <string_view>
#include <vector>

namespace rhi
{
    class ShaderBundle;
}

namespace bootstrap
{
    struct BootResult;
    struct BundleRequest;
    class CoreStage;
    // LoadedBundle.hpp defines it. It holds a ShaderBundle by value, so this header does not include it
    struct LoadedBundle;

    /** @brief The content stage is responsible for loading binary content that doesn't need to use the GPU
      * in any way - think decoding textures, reading shader bundles, localization data, etc.
      *
      * Primarily,it is intended currently to open shader bundles and parse the features and capabilities
      * they declare, so that we can select and enable as many supported features, extensions, and properties
      * as possible. This is even more important on WebGPU, where we also need to actually manage and set 
      * limits on things like thread counts, binding layouts, and resource sizes explicitly!
    */
    class ContentStage
    {
    public:
        ContentStage() noexcept;
        /** @brief Calls Stop. */
        ~ContentStage();
        // DeviceRequirements and later stages store views into the bundles
        ContentStage(const ContentStage&) = delete;
        ContentStage& operator=(const ContentStage&) = delete;
        ContentStage(ContentStage&&) = delete;
        ContentStage& operator=(ContentStage&&) = delete;

        /** @brief Opens `app_bundles`, then the bundles of each selected module, in list order. A request that
         *  matches an earlier request opens nothing. Fails with BundleNameConflict when the file name and profile
         *  match an earlier request and the build-tree path does not, with BundleNotFound when the file is not
         *  beside the executable and not at its build-tree path, and with BundleInvalid when the bundle does not
         *  open, or when a module in it has no code facts. On a failure, closes what it opened. `core` and
         *  `app_bundles` must outlive this stage, as a stage local and a constant array do. */
        [[nodiscard]] BootResult Start(const CoreStage& _core, std::span<const BundleRequest> app_bundles);
        /** @brief Closes every bundle. */
        void Stop() noexcept;

        [[nodiscard]] bool Started() const noexcept;
        [[nodiscard]] const CoreStage& Core() const noexcept;
        /** @brief Returns the open bundles, in the order they were opened. */
        [[nodiscard]] std::span<const LoadedBundle> Bundles() const noexcept;
        /** @brief Returns the open bundle with this request file name, or null. */
        [[nodiscard]] const rhi::ShaderBundle* FindBundle(std::string_view file_name) const noexcept;

    private:
        [[nodiscard]] BootResult openBundle(const BundleRequest& request, std::string_view requested_by);

        const CoreStage* core{ nullptr };
        std::vector<LoadedBundle> bundles;
    };
}

#endif // !DIAMOND_DOGS_BOOTSTRAP_CONTENT_STAGE_HPP
