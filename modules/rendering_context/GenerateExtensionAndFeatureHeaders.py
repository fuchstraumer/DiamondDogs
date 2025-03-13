#!/usr/bin/env python3
# Parse vk.xml to generate a header of available extensions for a platform, relevant queryable names,
# and generate a hidden-behind-source list of features of those extensions so that we can query those too
# Guidance from https://www.anteru.net/blog/2018/codegen-for-fast-vulkan/

from xml.etree import ElementTree
import argparse
import pathlib

# get list of current vulkan version names
def GetVersionList(tree):
    '''
    Returns list of current vulkan versions, excluding the safety critical version
    and the 1.0 version, as that isn't relevant to extensions and features.
    Args:
        tree: xml.etree.ElementTree object containing the vk.xml data
    Returns:
        list: list of version elements from vk.xml
    '''
    result = []
    versions = tree.findall('./feature')
    for version in versions:
        version_name = version.get('name')
        if 'VKSC_VERSION_1_0' in version_name:
            continue
        elif 'VK_VERSION_1_0' in version_name:
            continue

        result.append(version)

    return result

def MakeVersionStrVersionNumber(version):
    '''
    Turns a version name string into a VK_VERSION_X_Y macro name (so it works as uint key to maps)
    Args:
        version: version name
    Returns:
        string: version macro name, which evaluates to a uint when compiled
    '''
    insertionPointIdx = version.find('_VERSION')
    if insertionPointIdx != -1:
        outputStr = version[:insertionPointIdx] + '_API' + version[insertionPointIdx:]
        return outputStr
    return version  # Return original if pattern not found

def RemoveExtensionsWithZeroVersion(extensions):
    '''
    Finds extensions that have a zero version field, meaning that theyre either long-deprecated
    or just unused. Either way, we shouldn't be including them in our master list or clientside
    query list
    Args:
        extensions: list of extension elements from vk.xml
    Returns:
        list: extensions with a zero version value
    '''
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

    # Remove extensions and print number removed
    numRemoved = len(extensionsToRemove)
    print(f'Removing {numRemoved} extensions with zero version')
    extensions[:] = [ext for ext in extensions if ext not in extensionsToRemove]


def FindAllPromotedExtensions(extensions, versions):
    '''
    In Vulkan, extensions are occasionally either promoted to a new alias (for various
    reasons), or promoted to a new version of the API as they are made core to that version.
    We grab both cases here.
    Args:
        extensions: list of extension elements from vk.xml
        versions: list of version elements from vk.xml
    Returns:
        dict: mapping old extension names to new extension names
        dict: mapping version names to lists of extensions promoted to core in that version
    '''
    promotedExtensions = {}
    versionPromotedExtensions = {}
    versionNames = [version.get('name') for version in versions]
    for version in versionNames:
        versionPromotedExtensions[version] = []

    for extension in extensions:
        promotedTo = extension.get('promotedto')
        # In newer schemas, some extensions use 'obsoleted_by' instead of 'promotedto'
        if promotedTo is None:
            promotedTo = extension.get('obsoleted_by')
            
        if promotedTo is not None and promotedTo not in versionNames:
            promotedExtensions[extension] = promotedTo
        elif promotedTo is not None and promotedTo in versionNames:
            versionPromotedExtensions[promotedTo].append(extension.get('name'))

    return promotedExtensions, versionPromotedExtensions

def RemoveAliasedExtensions(extensions, aliasedExtensions):
    '''
    Removes aliased extensions from the master list. We've already copied their values
    to the container returned from the above function though. We use this to construct the
    remapping table for API users, where aliased extensions.... map to their current alias :)
    Args:
        extensions: master list of extension elements from vk.xml. modified by this function
        aliasedExtensions: dict mapping old extension names to new extension names
    '''
    for aliasedExtension in aliasedExtensions.keys():
        if aliasedExtension in extensions:
            extensions.remove(aliasedExtension)

# 0x55555555
def FindAllExtensionsDependencies(extensions, versions):
    '''
    Find all dependencies required by an extension for a given version of the API.
    Args:
        extensions: list of extension elements from vk.xml
        versions: list of version elements from vk.xml
    Returns:
        dict: mapping version names to a dict of extension names to lists of dependencies, for that extension and version
              effectively, it maps a version to a list of extensions that have dependencies *relating* to that version
              there's a special "NO_VERSION" key that covers extensions that don't have any version dependencies, just extensions
        int: maximum overall number of dependencies found for any one extension
             (used to set the size of the std::array in generated header)
    '''
    versionDependencies = {}
    # Map of version name to list of extensions that have dependencies *relating* to that version
    # They could be just that version, or a list of extensions required alongside this version
    versionNameList = [version.get('name') for version in versions]
    versionNameList.append('NO_VERSION')
    for versionName in versionNameList:
        versionDependencies[versionName] = {}

    mostDeps = 0

    def CleanDependencyStr(dep):
        return dep.strip().strip('()').strip()

    for extension in extensions:
        extensionName = extension.get('name')
        dependenciesAttrib = extension.get('depends')
        
        if dependenciesAttrib is not None:
            # Split by commas (OR dependencies) and process each group
            dependency_groups = dependenciesAttrib.split(',')
            
            for group in dependency_groups:
                # Split by + (AND dependencies)
                and_dependencies = group.strip().split('+')
                # Check if any of the AND dependencies is a version dependency
                is_version_dependency = False
                version_name = 'NO_VERSION'
                for dep in and_dependencies:
                    clean_dep = CleanDependencyStr(dep)
                    if any(version.get('name') == clean_dep for version in versions):
                        is_version_dependency = True
                        version_name = clean_dep
                        break
                
                # If this is a version dependency, we need to notate the version alongside the list of extension dependencies
                if is_version_dependency and len(and_dependencies) == 1:
                    # Empty list, but it still tells us something. Use 0x55555555 as a sentinel value
                    if versionDependencies[version_name].get(extensionName) is None:
                        versionDependencies[version_name][extensionName] = []
                    versionDependencies[version_name][extensionName].append("0x55555555")
                    continue
                elif is_version_dependency and len(and_dependencies) > 1:
                    # Remove version from list of and_dependencies
                    # Remove the version dependency from and_dependencies, accounting for possible formatting
                    and_dependencies = [dep for dep in and_dependencies if CleanDependencyStr(dep) != version_name]
                    for dep in and_dependencies:
                        # Remove any parentheses and whitespace
                        clean_dep = CleanDependencyStr(dep)
                        if versionDependencies[version_name].get(extensionName) is None:
                            versionDependencies[version_name][extensionName] = []
                        versionDependencies[version_name][extensionName].append(clean_dep)
                elif not is_version_dependency:
                    # No version dependency, just extensions
                    for dep in and_dependencies:
                        # Remove any parentheses and whitespace
                        clean_dep = CleanDependencyStr(dep)
                        if versionDependencies['NO_VERSION'].get(extensionName) is None:
                            versionDependencies['NO_VERSION'][extensionName] = []
                        versionDependencies['NO_VERSION'][extensionName].append(clean_dep)
                
    # Find the maximum number of dependencies across all versions
    for version_name, extensions_dict in versionDependencies.items():
        for ext_name, deps_list in extensions_dict.items():
            mostDeps = max(mostDeps, len(deps_list))


    return versionDependencies, mostDeps

def CreateFileHeader(tree, fileStream):
    '''
    Creates the file header, including the include guard and includes.
    Hashes the vk.xml data to create a unique include guard based on the version of the API.
    Args:
        tree: xml.etree.ElementTree object containing the vk.xml data
        fileStream: file stream to write to
    '''
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
    '''
    Writes the master extension name table, containing the actual strings we use.
    Args:
        extensions: list of extension elements from vk.xml
        fileStream: file stream to write to
    Returns:
        dict: mapping extension names to their index in the masterExtensionNameTable
    '''
    nameTable = []

    for extension in extensions:
        extensionName = extension.get('name')
        nameTable.append(extensionName)

    print('constexpr static std::array<const char*, ' + str(len(nameTable)) + '> masterExtensionNameTable', file=fileStream)
    print('{', file=fileStream)

    for name in nameTable:
        print('    \"' + name + "\",", file=fileStream)

    print('};\n', file=fileStream)

def WriteAliasedExtensionNameTable(aliasedExtensions, fileStream):
    '''
    Writes out a table for the aliased extensions, containing their names as raw strings.
    Can't be string_view since these extensions are no longer part of the master list.
    Args:
        aliasedExtensions: dict mapping old extension names to new extension names
        fileStream: file stream to write to
    Returns:
        dict: mapping extension names to their index in the aliasedExtensionNameTable
    '''
    aliasedNameDataIndices = {}
    nameTable = []

    for extension in aliasedExtensions.keys():
        extensionName = extension.get('name')
        aliasedNameDataIndices[extensionName] = len(nameTable)
        nameTable.append(extensionName)
        
    print('constexpr static std::array<const char*, ' + str(len(nameTable)) + '> aliasedExtensionNameTable', file=fileStream)
    print('{', file=fileStream)

    for name in nameTable:
        print('    \"' + name + '\",', file=fileStream)
    
    print('};\n', file=fileStream)

    return aliasedNameDataIndices

def GetNameToIndexDict(extensions):
    '''
    Generates a mapping of extension names to their index in the masterExtensionNameTable.
    Args:
        extensions: list of extension elements from vk.xml
    Returns:
        dict: mapping extension names to their index in the masterExtensionNameTable
    '''
    result = {}
    for idx, extension in enumerate(extensions):
        result[extension.get('name')] = idx
    return result;

# Write out the final extension index lookup table. This uses the arrays of strings we already built to allow us to use
# string view here, without having to worry about backing storage for said strings we're viewing. Those strings are all baked
# in as constexpr arrays. 
def WriteExtensionIndexLookupTable(extensions, masterNameToIndexDict, aliasedExtensions, aliasedNameToIndexDict, fileStream):
    '''
    Writes out the final extension index lookup table. This uses the arrays of strings we already built to allow us to use
    string view here, without having to worry about backing storage for said strings we're viewing. Those strings are all baked
    in as constexpr arrays. 
    Args:
        extensions: list of extension elements from vk.xml
        masterNameToIndexDict: mapping of extension names to their index in the masterExtensionNameTable
        aliasedExtensions: dict mapping old extension names to new extension names
        aliasedNameToIndexDict: mapping of aliased extension names to their index in the aliasedExtensionNameTable
    '''
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

def WriteDeviceOrInstanceExtensionTable(extensions, extensionIdxDict, fileStream, extensionTypeToWrite):
    '''
    Writes out a table of extensions that are instance extensions.
    '''
    extensionIndices = []
    for extension in extensions:
        extensionName = extension.get('name')
        extensionType = extension.get('type')
        if extensionType == extensionTypeToWrite and extensionName in extensionIdxDict:
            extensionIndices.append(extensionIdxDict[extensionName])

    print('// Table of ' + extensionTypeToWrite + ' extension indices, indexed by the extension name', file=fileStream)
    print('static const std::array<size_t, ' + str(len(extensionIndices)) + '> ' + extensionTypeToWrite + 'ExtensionTable', file=fileStream)
    print('{', file=fileStream)
    for idx, extensionIdx in enumerate(extensionIndices):
        print('    ' + str(extensionIdx) + ',', file=fileStream)
    print('};\n', file=fileStream)

def WriteExtensionDependencyTable(extensions, versions, versionAndExtensionDependencies, extensionIdxDict, fileStream, maxDeps):
    '''
    Writes out a table mapping extension indices to an array of it's dependencies. Dependency array is a std::array with a capacity
    set dynamically based on the max amount of dependencies we found for any one extension. For any extension that doesn't have that
    many deps, we just write UINT_MAX/std::numeric_limits<size_t>::max() to the slot.

    This table is partitioned by version, with "NO_VERSION" extensions coming first followed by versioned extensions. We write a followup
    array that maps version names to the index of where the versioned extensions begin in the dependency table.

    Args:
        extensions: list of extension elements from vk.xml
        versions: list of version elements from vk.xml
        versionAndExtensionDependencies: dict mapping version names to a dict of extension names to lists of dependencies
        extensionIdxDict: mapping of extension names to their index in the masterExtensionNameTable
        fileStream: file stream to write to
        maxDeps: maximum number of dependencies found for any one extension
    '''
    dependencyIndexType = 'size_t'
    invalidDepIdx = 'std::numeric_limits<' + dependencyIndexType + '>::max()'
    
    # Build a list of VK_MAKE_VERSION() macros for each version in versions
    versionMacros = {}
    for version in versions:
        if version.get('name') == 'NO_VERSION':
            continue
        versionName = version.get('name')
        versionMacros[versionName] = MakeVersionStrVersionNumber(versionName)

    dependencyVectorStr = 'std::array<size_t, ' + str(maxDeps) + '>'
    print('// Extension dependency table - partitioned by version, with \"NO_VERSION\" extensions coming first followed by versioned extensions', file=fileStream)
    print('// See array below for indices to where extensions for each version begin in the array', file=fileStream)
    print('constexpr static std::array<' + dependencyVectorStr + ', ' + str(len(extensions)) + '> extensionDependencyToExtensionsTable', file=fileStream)
    print('{', file=fileStream)

    # Build the dependency table, partitioned by version
    versionPartionedDependencyIndexMap = {}
    for version, extensionsAndDeps in versionAndExtensionDependencies.items():    
        dependencyIndexMap = {}
        for extensionName, dependencies in extensionsAndDeps.items():
            dependencyIndices = []
            # Add the version macro at the front of the dependency list
            if version != "NO_VERSION":
                dependencyIndices.insert(0, versionMacros[version])
            # Now handle all extension dependencies
            for dependency in dependencies:
                if dependency in extensionIdxDict:
                    dependencyIndices.append(extensionIdxDict[dependency])
                elif dependency == "0x55555555":
                    # This is a sentinel value indicating that the extension has no extension dependencies, just a version dependency
                    # We already added the version macro at the front, so we don't need to do anything here
                    pass
                else:
                    print(f"Warning: Dependency {dependency} for {extensionName} not found in extension index dictionary")
            dependencyIndexMap[extensionName] = dependencyIndices
        versionPartionedDependencyIndexMap[version] = dependencyIndexMap

    # Sort and partition the dependencyIndexMap by version, with "NO_VERSION" extensions coming first followed by versioned extensions
    # Generate a dict of version name to index of where the versioned extensions begin in the dependency table

    # Sort the versionPartionedDependencyIndexMap by version name
    # Custom sort function to ensure NO_VERSION comes first, then sort the rest by version
    def version_sort_key(item):
        version_name = item[0]
        if version_name == "NO_VERSION":
            return ""  # Empty string sorts before any other string
        return version_name
    
    versionPartionedDependencyIndexMap = sorted(versionPartionedDependencyIndexMap.items(), key=version_sort_key)

    versionedExtensionStartIndices = {}
    offsetSoFar = 0
    for version, dependencyIndexMap in versionPartionedDependencyIndexMap:
        versionedExtensionStartIndices[version] = offsetSoFar
        offsetSoFar += len(dependencyIndexMap)
        

    for version, dependencyIndexMap in versionPartionedDependencyIndexMap:
        versionString = "    // Start of extensions for version: " + version
        print(versionString, file=fileStream)
        for extensionName, dependencyIndices in dependencyIndexMap.items():
            print('    // Extension: ' + extensionName, file=fileStream)
            if extensionName in extensionsAndDeps:
                # Add version name at the front of the dependency list if it's not NO_VERSION
                dependencies = extensionsAndDeps[extensionName]
                if version != "NO_VERSION":
                    dependencies = [f"{version}"] + dependencies
                dependencyNameString = ', '.join(dependencies)
            else:
                # If no dependencies, still show the version if applicable
                dependencyNameString = f"{version}" if version != "NO_VERSION" else "None"
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
    
    # Table of indices to where the versioned extensions begin in the dependency table
    print('// Table of indices to where the versioned extensions begin in the dependency table above', file=fileStream)
    print('static const std::array<size_t, ' + str(len(versionedExtensionStartIndices)) + '> versionedExtensionStartIndices', file=fileStream)
    print('{', file=fileStream)
    for version, index in versionedExtensionStartIndices.items():
        versionString = "    // Start of extensions for version: " + version
        print(versionString, file=fileStream)
        print('    ' + str(index) + ',', file=fileStream)
    print('};\n', file=fileStream)


def PrintVersionedExtensions(promotedVersionedExtensions, extensionIdxDict, fileStream):
    print('// Table of versioned extensions, indexed by version name->version number macro. If an extension index is in this map for a\n// version, it means that extension is core of that version.\n', file=fileStream)
    versionedExtensionVecStr = 'std::vector<size_t>'
    print('static const std::unordered_map<size_t, ' + versionedExtensionVecStr + '> versionedExtensionsMap', file=fileStream)
    print('{', file=fileStream)

    for version, versionedExtensions in promotedVersionedExtensions.items():
        if not versionedExtensions:  # Skip empty lists
            continue

        print('    // Version: ' + version, file=fileStream)
        
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

def FindPropertyStructs(tree):
    """
    Find all property structs used to indicate the properties an extension has that we will query support of.
    Parses the tree and finds all the structs, setting their type enum to the correct value. Return this 
    list of structs, which will later be associated to their extensions and grouped that way.
    """
    property_structs = []
    all_structs = tree.findall('.//type[@category="struct"][@structextends="VkPhysicalDeviceProperties2"]')

    for struct in all_structs:
        struct_name = struct.get('name')
        struct_type_enum = None
        for member in struct.findall('.//member'):
            type = member.find('type')
            if type is not None and type.text == 'VkStructureType':
                struct_type_enum = member.get('values')
                break
        
        if struct_type_enum is None:
            print(f"Warning: Could not find structure type enum for {struct_name}")
            continue

        property_structs.append({
            'name': struct_name,
            'type_enum': struct_type_enum
        })
    
    return property_structs

def FindFeatureStructs(tree):
    """
    Find all feature structs used to indicate the features an extension has that we will query support of.
    Parses the tree and finds all the structs, setting their type enum to the correct value. Return this 
    list of structs, which will later be associated to their extensions and grouped that way.
    """
    feature_structs = []
    
    # First, find all structures that have "features" in their name or are known feature structs
    all_structs = tree.findall('.//type[@category="struct"][@structextends="VkPhysicalDeviceFeatures2,VkDeviceCreateInfo"]')
    
    for struct in all_structs:
        struct_name = struct.get('name')
        # Find the structure type enum by looking for the VkStructureType value in the struct definition
        struct_type_enum = None
        for member in struct.findall('.//member'):
            type = member.find('type')
            if type is not None and type.text == 'VkStructureType':
                struct_type_enum = member.get('values')
                break
        
        if struct_type_enum is None:
            print(f"Warning: Could not find structure type enum for {struct_name}")
            continue
        
        feature_structs.append({
            'name': struct_name,
            'type_enum': struct_type_enum
        })
    
        
    return feature_structs

def GroupStructsByExtension(structs, extensions, versions, struct_type):
    """
    Iterates through list of feature structs, and groups them by the extension they are associated with.
    Returns a dictionary where the keys are extension names and the values are lists of feature structs
    associated with that extension. Should only ever be one struct per extension, however
    """
    grouped_structs = {}
    # unfortunate issue: we're just going to have to manually update this list of version extensions
    # as new core versions are released.
    version_feature_structs = {
        'VK_VERSION_1_1': 'VkPhysicalDeviceVulkan11Features',
        'VK_VERSION_1_2': 'VkPhysicalDeviceVulkan12Features', 
        'VK_VERSION_1_3': 'VkPhysicalDeviceVulkan13Features',
        'VK_VERSION_1_4': 'VkPhysicalDeviceVulkan14Features'
    }
    version_property_structs = {
        'VK_VERSION_1_1': 'VkPhysicalDeviceVulkan11Properties',
        'VK_VERSION_1_2': 'VkPhysicalDeviceVulkan12Properties',
        'VK_VERSION_1_3': 'VkPhysicalDeviceVulkan13Properties',
        'VK_VERSION_1_4': 'VkPhysicalDeviceVulkan14Properties'
    }

    if struct_type == "Features":
        # Add version feature structs to grouped_structs
        for version_name, struct_name in version_feature_structs.items():
            # Find the struct in the list of structs
            matching_structs = [s for s in structs if s['name'] == struct_name]
            if matching_structs:
                grouped_structs[version_name] = matching_structs[0]
                # Remove from structs list so we don't process it again
                structs.remove(matching_structs[0])
    elif struct_type == "Properties":
        # Add version property structs to grouped_structs
        for version_name, struct_name in version_property_structs.items():
            # Find the struct in the list of structs
            matching_structs = [s for s in structs if s['name'] == struct_name]
            if matching_structs:
                grouped_structs[version_name] = matching_structs[0]
                # Remove from structs list so we don't process it again
                structs.remove(matching_structs[0])
    

    for extension in extensions:
        extension_types = extension.findall('.//type')
        extension_feature_structs = []
        for extension_type in extension_types:
            extension_type_name = extension_type.get('name')
            # Check if this type is a feature struct by looking for "Features" in the name
            if struct_type in extension_type_name:
                extension_feature_structs.append(extension_type_name)
    
        for extension_feature_struct in extension_feature_structs:
            for feature_struct in structs:
                if feature_struct['name'] == extension_feature_struct:
                    grouped_structs[extension.get('name')] = feature_struct
                    # Remove the feature struct from the list so we don't process it again
                    structs.remove(feature_struct)
                    extension_feature_structs.remove(extension_feature_struct)
                    break

    return grouped_structs

def WriteQueriedDeviceFeaturesStructs(tree, extensions, versions, fileStream):
    """
    Generate a QueriedDeviceFeatures struct that contains all feature structs
    from the supported extensions, with proper pNext chaining.
    """
    feature_structs = FindFeatureStructs(tree)
    grouped_feature_structs = GroupStructsByExtension(feature_structs, extensions, versions, "Features")
    property_structs = FindPropertyStructs(tree)
    grouped_property_structs = GroupStructsByExtension(property_structs, extensions, versions, "Properties")
    
    # Add VkPhysicalDeviceFeatures2 as the base struct at the end
    feature_structs.append({
        'name': 'VkPhysicalDeviceFeatures2',
        'type_enum': 'VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2',
        'extension': None
    })
    
    print('\n// Generated QueriedDeviceFeatures struct', file=fileStream)
    print('struct QueriedDeviceFeatures', file=fileStream)
    print('{', file=fileStream)
    
    # Generate the struct members in reverse order (from last to first in the chain)
    for i in range(len(feature_structs) - 1, -1, -1):
        struct = feature_structs[i]
        var_name = struct['name'][2:] if struct['name'].startswith('Vk') else struct['name']
        
        # Convert to pascalCase for private member variables (following the style guidelines)
        var_name = var_name[0].lower() + var_name[1:]
        
        # For the last struct in the chain, pNext is nullptr
        if i == len(feature_structs) - 1:
            if struct['name'] == 'VkPhysicalDeviceFeatures2':
                print(f'    {struct["name"]} {var_name}', file=fileStream)
                print(f'    {{', file=fileStream)
                print(f'        {struct["type_enum"]},', file=fileStream)
                print(f'        nullptr,', file=fileStream)
                print(f'        VkPhysicalDeviceFeatures{{}}', file=fileStream)
                print(f'    }};', file=fileStream)
            else:
                print(f'    {struct["name"]} {var_name}', file=fileStream)
                print(f'    {{', file=fileStream)
                print(f'        {struct["type_enum"]},', file=fileStream)
                print(f'        nullptr', file=fileStream)
                print(f'    }};', file=fileStream)
        else:
            # For other structs, pNext points to the next struct in the chain
            next_struct = feature_structs[i + 1]
            next_var_name = next_struct['name'][2:] if next_struct['name'].startswith('Vk') else next_struct['name']
            next_var_name = next_var_name[0].lower() + next_var_name[1:]
            
            print(f'    {struct["name"]} {var_name}', file=fileStream)
            print(f'    {{', file=fileStream)
            print(f'        {struct["type_enum"]},', file=fileStream)
            print(f'        &{next_var_name}', file=fileStream)
            print(f'    }};', file=fileStream)
    
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

        # Find all aliased extensions, ones that have extensions that replace them with new names
        promotedAliasedExtensions, promotedVersionedExtensions = FindAllPromotedExtensions(extensions, versions)
        print(f"Found {len(promotedAliasedExtensions)} aliased extensions")
        
        RemoveAliasedExtensions(extensions, promotedAliasedExtensions)
        print(f"After removing aliased and zero-version extensions: {len(extensions)} extensions remain")
        
        outputPath = pathlib.Path(args.outputDir) / 'GeneratedExtensionHeader.hpp'
        print(f"Generating header file at {outputPath}")
        
        fileStream = open(outputPath, 'w', encoding='utf-8')
        CreateFileHeader(tree, fileStream)
        WriteMasterExtensionNameTable(extensions, fileStream)
        masterExtensionIdxDict = GetNameToIndexDict(extensions)
        aliasedExtensionIdxDict = WriteAliasedExtensionNameTable(promotedAliasedExtensions, fileStream)

        WriteExtensionIndexLookupTable(extensions, masterExtensionIdxDict, promotedAliasedExtensions, aliasedExtensionIdxDict, fileStream)

        WriteDeviceOrInstanceExtensionTable(extensions, masterExtensionIdxDict, fileStream, 'device')
        WriteDeviceOrInstanceExtensionTable(extensions, masterExtensionIdxDict, fileStream, 'instance')

        versionDependencies, mostDeps = FindAllExtensionsDependencies(extensions, versions)

        WriteExtensionDependencyTable(extensions, versions, versionDependencies, masterExtensionIdxDict, fileStream, mostDeps)

        PrintVersionedExtensions(promotedVersionedExtensions, masterExtensionIdxDict, fileStream)

        FinalizeFile(fileStream)
        print("Successfully generated header file")
        
    except Exception as e:
        print(f"Error generating header: {e}")
        import traceback
        traceback.print_exc()
        exit(1)
