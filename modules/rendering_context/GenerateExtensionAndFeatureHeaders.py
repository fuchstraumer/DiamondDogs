#!/usr/bin/env python3
# Parse vk.xml to generate a header of available extensions for a platform, relevant queryable names,
# and generate a hidden-behind-source list of features of those extensions so that we can query those too
# Guidance from https://www.anteru.net/blog/2018/codegen-for-fast-vulkan/

from xml.etree import ElementTree
import argparse
import pathlib

# get list of current vulkan version names
def GetVersionList(tree):
    result = []
    versions = tree.findall('./feature')
    for version in versions:
        result.append(version.get('name'))

    # Vulkan safety critical version. Remove, as it's unused in our game-focused setup
    # Only remove if it exists in the list
    if 'VKSC_VERSION_1_0' in result:
        result.remove('VKSC_VERSION_1_0')
    # Remove 1.0, as it's not one we use and breaks stuff
    if 'VK_VERSION_1_0' in result:
        result.remove('VK_VERSION_1_0')

    return result

# Finds extensions that have a zero version field, meaning that theyre either long-deprecated
# or just unused. Either way, we shouldn't be including them in our master list or clientside
# query list
def RemoveExtensionsWithZeroVersion(extensions):
    extensionsToRemove = []
    for extension in extensions:
        extensionName = extension.get('name').upper()
        nameWeWant = extensionName + '_SPEC_VERSION'
        
        # First try to find the version in the direct enum
        extensionVersion = extension.find('./require/enum[@name="{}"]'.format(nameWeWant))
        
        # If not found, try to find it in the registry
        if extensionVersion is None:
            # Some extensions might not have a direct version enum, check if it's supported
            supported = extension.get('supported')
            if supported == 'disabled' or supported == 'vulkansc':
                extensionsToRemove.append(extension)
                continue
        elif extensionVersion.get('value') == '0':
            extensionsToRemove.append(extension)

    for zeroExtension in extensionsToRemove:
        extensions.remove(zeroExtension)


# Find promoted extensions - ones promoted to new aliases
def FindExtensionsPromotedToNewAliases(extensions, versions):
    
    promotedExtensions = {}

    for extension in extensions:
        promotedTo = extension.get('promotedto')
        # In newer schemas, some extensions use 'obsoleted_by' instead of 'promotedto'
        if promotedTo is None:
            promotedTo = extension.get('obsoleted_by')
            
        if promotedTo is not None and promotedTo not in versions:
            promotedExtensions[extension] = promotedTo

    return promotedExtensions

# Remove the promoted extensions from the master list: we've already copied their values
# to the container returned from the above function though. We use this to construct the
# remapping table for API users, where aliased extensions.... map to their current alias :)
def RemoveAliasedExtensions(extensions, aliasedExtensions):
    for aliasedExtension in aliasedExtensions.keys():
        if aliasedExtension in extensions:
            extensions.remove(aliasedExtension)

# Collate extensions promoted to new version of Vulkan, and collate this
def CollateExtensionsPromotedToNewVkVersion(extensions, versions):
    versionPromotedExtensions = { version:[] for version in versions }

    for extension in extensions:
        promotedTo = extension.get('promotedto')
        if promotedTo is not None:
            if promotedTo in versions:
                versionPromotedExtensions[promotedTo].append(extension.get('name'))

    return versionPromotedExtensions

def FindAllExtensionsDependencies(extensions, versions):
    # Extension dependencies required for a version of the API: as in, lists of per-version deps
    extensionDependencies = {}
    mostDeps = 0

    for extension in extensions:
        extensionName = extension.get('name')
        dependenciesAttrib = extension.get('depends')
        
        if dependenciesAttrib is not None:
            dependencies = []
            
            # Split by commas (OR dependencies) and process each group
            dependency_groups = dependenciesAttrib.split(',')
            
            for group in dependency_groups:
                # Split by + (AND dependencies)
                and_dependencies = group.strip().split('+')
                
                for dep in and_dependencies:
                    # Remove any parentheses and whitespace
                    clean_dep = dep.strip().strip('()').strip()
                    
                    # Skip if it's a version dependency (we only care about extension dependencies)
                    if any(version == clean_dep for version in versions):
                        continue
                    
                    # Add to dependencies if it's an extension
                    if clean_dep.startswith('VK_'):
                        dependencies.append(clean_dep)
            
            if dependencies:
                extensionDependencies[extensionName] = dependencies
                mostDeps = max(mostDeps, len(dependencies))

    return extensionDependencies, mostDeps

def CreateFileHeader(tree, fileStream):
    import hashlib
    includeUuid = hashlib.sha256(ElementTree.tostring (tree)).hexdigest().upper ()
    print(f'#ifndef VK_EXTENSION_WRANGLER_LOOKUPS_{includeUuid}', file=fileStream)
    print(f'#define VK_EXTENSION_WRANGLER_LOOKUPS_{includeUuid}', file=fileStream)
    print('#include <cstdint>\n#include <array>\n#include <string_view>', file=fileStream)
    print('#include <unordered_map>\n#include <limits>', file=fileStream)
    print('#include <vulkan/vulkan_core.h>\n', file=fileStream)

# This is the "master" table, containing the actual strings we use. It contains all the currently
# valid extensions, with aliased extensions stripped out and zero version extensions stripped out. 
# We use this as what things remap to
def WriteMasterExtensionNameTable(extensions, fileStream):

    nameTable = []

    for extension in extensions:
        extensionName = extension.get('name')
        nameTable.append(extensionName)

    print('constexpr static std::array<const char*, ' + str(len(nameTable)) + '> masterExtensionNameTable', file=fileStream)
    print('{', file=fileStream)

    for name in nameTable:
        print('    \"' + name + "\",", file=fileStream)

    print('};\n', file=fileStream)

# Writes out a table for the aliased extensions, containing their raw strings (since we use string_view-
# elsewhere, we need to make sure that the backing data does exist. So we shove it in a constexpr array like so
def WriteAliasedExtensionTable(aliasedExtensions, fileStream):

    aliasedNameDataIndices = {}
    nameTable = []

    for extension, promotedTo in aliasedExtensions.items():
        extensionName = extension.get('name')
        aliasedNameDataIndices[extensionName] = len(nameTable)
        nameTable.append(extensionName)
    
    if not nameTable:  # Handle empty table case
        print('constexpr static std::array<const char*, 1> aliasedExtensionNameTable', file=fileStream)
        print('{', file=fileStream)
        print('    \"VK_EMPTY_PLACEHOLDER\"', file=fileStream)
        print('};\n', file=fileStream)
        return aliasedNameDataIndices
        
    print('constexpr static std::array<const char*, ' + str(len(nameTable)) + '> aliasedExtensionNameTable', file=fileStream)
    print('{', file=fileStream)

    for name in nameTable:
        print('    \"' + name + '\",', file=fileStream)
    
    print('};\n', file=fileStream)

    return aliasedNameDataIndices

# With the master list output, we now generate the Python-side 
def GetNameToIndexDict(extensions):
    result = {}
    for idx, extension in enumerate(extensions):
        result[extension.get('name')] = idx
    return result;

# Write out the final extension index lookup table. This uses the arrays of strings we already built to allow us to use
# string view here, without having to worry about backing storage for said strings we're viewing. Those strings are all baked
# in as constexpr arrays. 
def WriteExtensionIndexLookupTable(extensions, masterNameToIndexDict, aliasedExtensions, aliasedNameToIndexDict, fileStream):
    print('static const std::unordered_map<std::string_view, size_t> extensionIndexLookupMap', file=fileStream)
    print('{', file=fileStream)
    
    for extension in extensions:
        extensionName = extension.get('name')
        if extensionName in masterNameToIndexDict:
            indexToMasterTable = masterNameToIndexDict[extensionName]
            print(
                '    { masterExtensionNameTable[' + str(indexToMasterTable) + '], ' +
                       str(indexToMasterTable) + ' }, //' + extensionName, file=fileStream)
        else:
            print(f"Warning: Extension {extensionName} not found in master name to index dictionary")

    for aliasedExtension, promotedTo in aliasedExtensions.items():
        extensionName = aliasedExtension.get('name')
        if extensionName in aliasedNameToIndexDict and promotedTo in masterNameToIndexDict:
            extensionIdx = str(aliasedNameToIndexDict[extensionName])
            masterExtensionIdx = str(masterNameToIndexDict[promotedTo])
            commentStr = " Alias " + extensionName + " -> Current " + promotedTo
            print(
                '    { aliasedExtensionNameTable[' + extensionIdx + '], ' +
                       masterExtensionIdx + ' }, //' + commentStr, file=fileStream)
        else:
            print(f"Warning: Aliased extension {extensionName} or promoted extension {promotedTo} not found in dictionaries")

    print('};\n', file=fileStream)

# Write out a table mapping extension indices to an array of it's dependencies. Dependency array is a std::array with a capacity
# set dynamically based on the max amount of dependencies we found for any one extension. For any extension that doesn't have that
# many deps, we just write UINT_MAX/std::numeric_limits<size_t>::max() to the slot
def WriteExtensionDependencyTable(extensions, extensionsAndDeps, extensionIdxDict, fileStream, maxDeps):
    # just in case I want to tweak this later
    dependencyIndexType = 'size_t'
    invalidDepIdx = 'std::numeric_limits<' + dependencyIndexType + '>::max()'
    dependencyVectorStr = 'std::array<size_t, ' + str(maxDeps) + '>'
    print('constexpr static std::array<' + dependencyVectorStr + ', ' + str(len(extensions)) + '> dependencyTable', file=fileStream)
    print('{', file=fileStream)
    
    dependencyIndexMap = {}

    for extension in extensions:
        extensionName = extension.get('name')
        if extensionName in extensionsAndDeps:
            extensionDependencies = extensionsAndDeps[extensionName]
            dependencyIndices = []
            for dependency in extensionDependencies:
                # Make sure the dependency exists in our extension index dictionary
                if dependency in extensionIdxDict:
                    dependencyIndices.append(extensionIdxDict[dependency])
                else:
                    print(f"Warning: Dependency {dependency} for {extensionName} not found in extension index dictionary")

            dependencyIndexMap[extensionName] = dependencyIndices
        else:
            dependencyIndexMap[extensionName] = []

    for extensionName, dependencyIndices in dependencyIndexMap.items():
        print('    // Extension: ' + extensionName, file=fileStream)
        if extensionName in extensionsAndDeps:
            dependencyNameString = ', '.join(extensionsAndDeps[extensionName])
            print('    // Dependencies: ' + dependencyNameString, file=fileStream)
            numUnusedDepSlots = maxDeps - len(dependencyIndices)
            unusedDepDummyIndices = []
            if numUnusedDepSlots > 0:
                unusedDepDummyIndices = [invalidDepIdx] * numUnusedDepSlots
            dependencyIndices = dependencyIndices + unusedDepDummyIndices
            dependencyIdxString = ', '.join(map(str,dependencyIndices))
            print('    ' + dependencyVectorStr + '{ ' + dependencyIdxString + ' },', file=fileStream)
        else:
            print('    // Dependencies: None', file=fileStream)
            idxString = ', '.join(map(str,[invalidDepIdx] * maxDeps))
            print('    ' + dependencyVectorStr + '{ ' + idxString + ' },', file=fileStream)
    
    print('};\n', file=fileStream)

def PrintVersionedExtensions(promotedVersionedExtensions, extensionIdxDict, fileStream):
    versionedExtensionVecStr = 'std::vector<size_t>'
    print('static const std::unordered_map<size_t, ' + versionedExtensionVecStr + '> versionedExtensionsMap', file=fileStream)
    print('{', file=fileStream)

    for version, versionedExtensions in promotedVersionedExtensions.items():
        if not versionedExtensions:  # Skip empty lists
            continue
            
        print('    // Version: ' + str(version), file=fileStream)
        
        def MakeVersionStrVersionNumber(version):
            insertionPointIdx = version.find('_VERSION')
            if insertionPointIdx != -1:
                outputStr = version[:insertionPointIdx] + '_API' + version[insertionPointIdx:]
                return outputStr
            return version  # Return original if pattern not found

        def GetExtensionIdx(extension, extensionIdxDict):
            if extension in extensionIdxDict:
                return extensionIdxDict[extension]
            print(f"Warning: Extension {extension} not found in extension index dictionary")
            return 0  # Default to 0 if not found

        indexList = [GetExtensionIdx(ext, extensionIdxDict) for ext in versionedExtensions]
        if indexList:  # Only print if we have valid indices
            indexStr = ', '.join(map(str, indexList))
            print('    { ' + MakeVersionStrVersionNumber(version) + ', ' + '{ ' + indexStr + ' } }, ', file=fileStream)

    print('};\n', file=fileStream)


def FinalizeFile(fileStream):
    print('#endif // END_OF_HEADER\n', file=fileStream)
    fileStream.close()

if __name__ == '__main__':

    parser = argparse.ArgumentParser()
    parser.add_argument('-specDir', help='Vulkan API directory')
    parser.add_argument('-outputDir', help='Output header directory')

    args = parser.parse_args()

    specDirPath = pathlib.Path(args.specDir)
    vkXmlPath = specDirPath / 'share/vulkan/registry/vk.xml'
    
    if not vkXmlPath.exists():
        print(f"Error: vk.xml not found at {vkXmlPath}")
        print("Please check your Vulkan SDK installation and path")
        exit(1)
    
    try:
        document = ElementTree.parse(vkXmlPath)
        tree = document.getroot()
        versions = GetVersionList(tree)
        extensions = tree.findall(f'./extensions/extension')

        print(f"Found {len(versions)} Vulkan versions and {len(extensions)} extensions")
        
        RemoveExtensionsWithZeroVersion(extensions)
        print(f"After removing zero-version extensions: {len(extensions)} extensions remain")

        # Find all aliased extensions, ones that have extensions that replace them with new names
        promotedAliasedExtensions = FindExtensionsPromotedToNewAliases(extensions, versions)
        print(f"Found {len(promotedAliasedExtensions)} aliased extensions")
        
        RemoveAliasedExtensions(extensions, promotedAliasedExtensions)
        print(f"After removing aliased extensions: {len(extensions)} extensions remain")

        promotedVersionedExtensions = CollateExtensionsPromotedToNewVkVersion(extensions, versions)
        
        outputPath = pathlib.Path(args.outputDir) / 'GeneratedExtensionHeader.hpp'
        print(f"Generating header file at {outputPath}")
        
        fileStream = open(outputPath, 'w', encoding='utf-8')
        CreateFileHeader(tree, fileStream)
        WriteMasterExtensionNameTable(extensions, fileStream)
        masterExtensionIdxDict = GetNameToIndexDict(extensions)
        aliasedExtensionIdxDict = WriteAliasedExtensionTable(promotedAliasedExtensions, fileStream)

        WriteExtensionIndexLookupTable(extensions, masterExtensionIdxDict, promotedAliasedExtensions, aliasedExtensionIdxDict, fileStream)

        extensionsAndDeps, mostDeps = FindAllExtensionsDependencies(extensions, versions)
        print(f"Found dependencies for {len(extensionsAndDeps)} extensions, max dependencies: {mostDeps}")

        WriteExtensionDependencyTable(extensions, extensionsAndDeps, masterExtensionIdxDict, fileStream, mostDeps)

        PrintVersionedExtensions(promotedVersionedExtensions, masterExtensionIdxDict, fileStream)

        FinalizeFile(fileStream)
        print("Successfully generated header file")
        
    except Exception as e:
        print(f"Error generating header: {e}")
        import traceback
        traceback.print_exc()
        exit(1)
