#!/usr/bin/env python3
# Parse vk.xml to generate a header of available extensions for a platform, relevant queryable names,
# and generate a hidden-behind-source list of features of those extensions so that we can query those too
# Guidance from https://www.anteru.net/blog/2018/codegen-for-fast-vulkan/

from xml.etree import ElementTree
import argparse
import sys
from collections import OrderedDict

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
    versionPromotedExtensions = {}

    for extension in extensions:
        promotedTo = extension.get('promotedTo')
        if promotedTo != None and versions.count(promotedTo) != 0:
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
    print('#include <unordered_map>\n#include <vector>', file=fileStream)
    #print(f'constexpr size_t vkExtensionWranglerXmlHash = 0x' + str(int(includeUuid)) + ';\n', file=fileStream)

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

def WriteExtensionDependencyTable(extensions, extensionsAndDeps, extensionIdxDict, fileStream, maxDeps):
    dependencyVectorStr = 'std::vector<size_t>'
    print('static const std::array<' + dependencyVectorStr + ', ' + str(len(extensions)) + '> dependencyTable', file=fileStream)
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
            dependencyIdxString = ', '.join(map(str,dependencyIndices))
            print('    ' + dependencyVectorStr + '{ ' + dependencyIdxString + ' },', file=fileStream)
        else:
            print('    // Dependencies: None', file=fileStream)
            print('    ' + dependencyVectorStr + '{ },', file=fileStream)
    
    print('};\n', file=fileStream)

def PrintVersionedExtensions(extensions, versions, extensionIdxDict, promotedVersionedExtensions, fileStream):
    print



def FinalizeFile(fileStream):
    print('#endif // END_OF_HEADER\n', file=fileStream)
    fileStream.close()



if __name__ == '__main__':
    document = ElementTree.parse('vk.xml')
    tree = document.getroot()
    versions = GetVersionList(tree)
    extensions = tree.findall(f'./extensions/extension')

    RemoveExtensionsWithZeroVersion(extensions)

    # Find all aliased extensions, ones that have extensions that rpelace them with new names
    promotedAliasedExtensions = FindExtensionsPromotedToNewAliases(extensions, versions)
    RemoveAliasedExtensions(extensions, promotedAliasedExtensions)

    promotedVersionedExtensions = CollateExtensionsPromotedToNewVkVersion(extensions, versions)
    

    fileStream = open('modules/rendering_context/src/GeneratedExtensionHeader.hpp', 'w', encoding='utf-8')
    CreateFileHeader(tree, fileStream)
    WriteMasterExtensionNameTable(extensions, fileStream)
    masterExtensionIdxDict = GetNameToIndexDict(extensions)
    aliasedExtensionIdxDict = WriteAliasedExtensionTable(promotedAliasedExtensions, fileStream)

    WriteExtensionIndexLookupTable(extensions, masterExtensionIdxDict, promotedAliasedExtensions, aliasedExtensionIdxDict, fileStream)

    extensionsAndDeps, mostDeps = FindExtensionsAndDependencies(extensions)

    WriteExtensionDependencyTable(extensions, extensionsAndDeps, masterExtensionIdxDict, fileStream, mostDeps)

    FinalizeFile(fileStream)

    result = {}
