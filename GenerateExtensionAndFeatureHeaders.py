#!/usr/bin/env python3
# Parse vk.xml to generate a header of available extensions for a platform, relevant queryable names,
# and generate a hidden-behind-source list of features of those extensions so that we can query those too
# Guidance from https://www.anteru.net/blog/2018/codegen-for-fast-vulkan/

from xml.etree import ElementTree
import argparse
import sys
import pathlib

# get list of current vulkan version names
def GetVersionList(tree):
    result = []
    versions = tree.findall('./feature')
    for version in versions:
        result.append(version.get('name'))

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
        extensionVersion = extension.find('./require/enum[@name="{}"]'.format(nameWeWant))
        if extensionVersion != None and extensionVersion.get('value') == '0':
            extensionsToRemove.append(extension)

    for zeroExtension in extensionsToRemove:
        extensions.remove(zeroExtension)


# Find promoted extensions - ones promoted to new aliases
def FindExtensionsPromotedToNewAliases(extensions, versions):
    
    promotedExtensions = {}

    for extension in extensions:
        promotedTo = extension.get('promotedto')
        if promotedTo != None and versions.count(promotedTo) == 0:
            promotedExtensions[extension] = promotedTo

    return promotedExtensions

# Remove the promoted extensions from the master list: we've already copied their values
# to the container returned from the above function though. We use this to construct the
# remapping table for API users, where aliased extensions.... map to their current alias :)
def RemoveAliasedExtensions(extensions, aliasedExtensions):
    for aliasedExtension in aliasedExtensions.keys():
        extensions.remove(aliasedExtension)

# Collate extensions promoted to new version of Vulkan, and collate this
def CollateExtensionsPromotedToNewVkVersion(extensions, versions):
    versionPromotedExtensions = { version:[] for version in versions }

    for extension in extensions:
        promotedTo = extension.get('promotedto')
        if promotedTo != None:
            if versions.count(promotedTo) != 0:
                versionPromotedExtensions[promotedTo].append(extension.get('name'))

    return versionPromotedExtensions

def FindExtensionsAndDependencies(extensions):
    result = {}

    mostDeps = 0

    for extension in extensions:
        dependenciesAttrib = extension.get('requires')
        if dependenciesAttrib != None:
            dependencies = dependenciesAttrib.split(',')
            result[extension.get('name')] = dependencies
            mostDeps = max(mostDeps, len(dependencies))

    return result, mostDeps

def CreateFileHeader(tree, fileStream):
    import hashlib
    includeUuid = hashlib.sha256(ElementTree.tostring (tree)).hexdigest().upper ()
    print(f'#ifndef VK_EXTENSION_WRANGLER_LOOKUPS_{includeUuid}', file=fileStream)
    print(f'#define VK_EXTENSION_WRANGLER_LOOKUPS_{includeUuid}', file=fileStream)
    print('#include <cstdint>\n#include <array>\n#include <string_view>', file=fileStream)
    print('#include <unordered_map>\n#include <vector>\n#include <limits>', file=fileStream)
    print('#include <vulkan/vulkan_core.h>\n', file=fileStream)

# This is the "master" table, containing the actual strings we use. It contains all the currently
# valid extensions, with aliased extensions stripped out and zero version extensions stripped out. 
# We use this as what things remap to
def WriteMasterExtensionNameTable(extensions, fileStream):

    nameTable = []

    for extension in extensions:
        extensionName = extension.get('name')
        nameTable.append(extensionName)

    print('constexpr std::array<const char*, ' + str(len(nameTable)) + '> masterExtensionNameTable', file=fileStream)
    print('{', file=fileStream)

    for name in nameTable:
        print('    \"' + name + "\",", file=fileStream)

    print('};\n', file=fileStream)

# Writes out a table for the aliased extensions, containing their raw strings (since we use string_view-
# elsewhere, we need to make sure that the backing data does exist. So we shove it in a constexpr array like so
def WriteAliasedExtensionTable(aliasedExtensions, fileStream):

    aliasedNameDataIndices = {}
    nameTable = []

    for extension in aliasedExtensions:
        extensionName = extension.get('name')
        aliasedNameDataIndices[extensionName] = len(nameTable)
        nameTable.append(extensionName)
    
    print('constexpr std::array<const char*, ' + str(len(nameTable)) + '> aliasedExtensionNameTable', file=fileStream)
    print('{', file=fileStream)

    for name in nameTable:
        print('    \"' + name + '\",', file=fileStream)
    
    print('};\n',file=fileStream)

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
        indexToMasterTable = masterNameToIndexDict[extensionName]
        print(
            '    { masterExtensionNameTable[' + str(indexToMasterTable) + '], ' +
                   str(indexToMasterTable) + ' }, //' + extensionName, file=fileStream)

    for aliasedExtension, masterExtensionName in aliasedExtensions.items():
        extensionName = aliasedExtension.get('name')
        extensionIdx = str(aliasedNameToIndexDict[extensionName])
        masterExtensionIdx = str(masterNameToIndexDict[masterExtensionName])
        commentStr = " Alias " + extensionName + " -> Current " + masterExtensionName
        print(
            '    { aliasedExtensionNameTable[' + extensionIdx + '], ' +
                   masterExtensionIdx + ' }, //' + commentStr, file=fileStream)

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
                dependencyIndices.append(extensionIdxDict[dependency])

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
        print('    // Version: ' + str(version), file=fileStream)
        
        def MakeVersionStrVersionNumber(version):
            insertionPointIdx = version.find('_VERSION')
            outputStr = version[:insertionPointIdx] + '_API' + version[insertionPointIdx:]
            return outputStr

        def GetExtensionIdx(extension, extensionIdxDict):
            return extensionIdxDict[extension]

        indexList = [ GetExtensionIdx(ext, extensionIdxDict) for ext in versionedExtensions]
        indexStr = ', '.join(map(str,indexList))
        print ('    { ' + MakeVersionStrVersionNumber(version) + ', ' + '{ ' + indexStr + ' } }, ', file=fileStream)

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
    
    document = ElementTree.parse(vkXmlPath)
    tree = document.getroot()
    versions = GetVersionList(tree)
    extensions = tree.findall(f'./extensions/extension')

    RemoveExtensionsWithZeroVersion(extensions)

    # Find all aliased extensions, ones that have extensions that rpelace them with new names
    promotedAliasedExtensions = FindExtensionsPromotedToNewAliases(extensions, versions)
    RemoveAliasedExtensions(extensions, promotedAliasedExtensions)

    promotedVersionedExtensions = CollateExtensionsPromotedToNewVkVersion(extensions, versions)
    
    fileStream = open(args.outputDir + '/GeneratedExtensionHeader.hpp', 'w', encoding='utf-8')
    CreateFileHeader(tree, fileStream)
    WriteMasterExtensionNameTable(extensions, fileStream)
    masterExtensionIdxDict = GetNameToIndexDict(extensions)
    aliasedExtensionIdxDict = WriteAliasedExtensionTable(promotedAliasedExtensions, fileStream)

    WriteExtensionIndexLookupTable(extensions, masterExtensionIdxDict, promotedAliasedExtensions, aliasedExtensionIdxDict, fileStream)

    extensionsAndDeps, mostDeps = FindExtensionsAndDependencies(extensions)

    WriteExtensionDependencyTable(extensions, extensionsAndDeps, masterExtensionIdxDict, fileStream, mostDeps)

    PrintVersionedExtensions(promotedVersionedExtensions, masterExtensionIdxDict, fileStream)

    FinalizeFile(fileStream)

    result = {}
