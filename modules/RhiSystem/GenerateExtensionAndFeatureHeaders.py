#!/usr/bin/env python3
# Parse vk.xml to generate a header of available extensions for a platform, relevant queryable names,
# and generate a hidden-behind-source list of features of those extensions so that we can query those too
# Guidance from https://www.anteru.net/blog/2018/codegen-for-fast-vulkan/

from xml.etree import ElementTree
import argparse
import re
import pathlib
from lark import Lark, Transformer

ExtensionDependencyGrammar = r"""
start: disjunction
disjunction: conjunction ("," conjunction)*
conjunction: factor ("+" factor)*
factor: VERSION | EXTENSION | "(" disjunction ")"
VERSION: /VK_VERSION_[0-9]+_[0-9]+/
EXTENSION: /VK_[A-Z]+_[A-Za-z0-9_]+/
%import common.WS
%ignore WS
"""

class ExtensionDependencyTransformer(Transformer):
    def disjunction(self, items):
        return {'type': 'OR', 'terms': items} if len(items) > 1 else items[0]
    
    def conjunction(self, items):
        return {'type': 'AND', 'factors': items} if len(items) > 1 else items[0]
    
    def factor(self, items):
        return items[0]

    def VERSION(self, token):
        return {'type': 'VERSION', 'name': token.value}
    
    def EXTENSION(self, token):
        return {'type': 'EXTENSION', 'name': token.value}
    
class ExtensionWithDependencies:
    '''
    Holds all the information we need to know about an extension when doing this processing, primarily for partitioning 
    data when writing out data tables. Also holds the dependencies as dict of ExtensionDependencies objects, one per
    version with dependency requirements. For most extensions, this will just have the ANY_VERSION entry, but when
    an extension has version-dependent requirements, this will have a mapping of version name to the dependencies
    for that version.
    '''
    # Extension name string from the xml object
    name = None

    # Index into the masterExtensionNameTable
    nameIndex = None

    # The extension object from vk.xml
    xmlObject = None
    
    # List of ExtensionDependencies objects, one per version with dependency reqs
    # Usually just has the ANY_VERSION entry for deps required no matter the version
    dependencies = {}
  
    # The platform this extension is relevant to
    platform = None
    
    # Whether this extension has no features, so we can skip querying them
    # when generating feature structs
    noFeatures = False

    # If it's a beta extension, which means it's hidden behind a define for beta features
    provisional = False

    # If this extension was promoted to a new name or version, this will be the new name or version it was
    # made core in
    promotedTo = None

    # Index to the aliased extensions entry in the name table
    aliasedTo = None

    # If this extension was obsoleted by a new extension, this will be the new extension it was obsoleted by
    obsoletedOrDeprecatedBy = None

def ConstructExtensionObjects(extensionXmlObjects):
    '''
    Constructs ExtensionWithDependencies objects from the list of extensions, filtering out
    platform-specific extensions that don't apply to the current platform (win32) and
    provisional extensions.
    '''
    extensionObjects = []
    
    # Define excluded platforms for Windows builds
    excluded_platforms = {'android', 'ios', 'macos', 'metal', 'wayland', 'xcb', 'xlib', 'directfb', 'fuchsia', 'ggp', 'qnx', 'screen', "sci", "ohos", "vi" }
    
    filtered_count = 0
    
    for extension in extensionXmlObjects:
        # Check if extension should be filtered out
        should_exclude = False
        
        # Filter out provisional extensions
        if extension.get('provisional') == 'true':
            should_exclude = True
            filtered_count += 1
            continue
            
        # Filter out platform-specific extensions not for our platform
        extension_platform = extension.get('platform')
        if extension_platform and extension_platform.lower() in excluded_platforms:
            should_exclude = True
            filtered_count += 1
            continue
            
        if should_exclude:
            continue
        
        extensionObject = ExtensionWithDependencies()
        extensionObject.name = extension.get('name')
        extensionObject.nameIndex = None
        extensionObject.xmlObject = extension
        extensionObject.dependencies = {}
        extensionPlatform = extension.get('platform')
        if extensionPlatform is None:
            extensionPlatform = 'win32'  # Default to win32 for our build platform
        extensionObject.platform = extensionPlatform
        extensionObject.noFeatures = extension.get('nofeatures') == 'true'
        extensionObject.provisional = extension.get('provisional') == 'true'

        extensionObject.promotedTo = extension.get('promotedto')
        extensionObject.obsoletedOrDeprecatedBy = extension.get('obsoletedby')
        if extensionObject.obsoletedOrDeprecatedBy is None:
            extensionObject.obsoletedOrDeprecatedBy = extension.get('deprecatedby')
        # Not sure why some of the spec extensions have an empty string for the obsoletedby attribute?
        if extensionObject.obsoletedOrDeprecatedBy == '':
            extensionObject.obsoletedOrDeprecatedBy = None

        extensionObjects.append(extensionObject)
    
    print(f"Filtered out {filtered_count} platform-specific or provisional extensions")
    return extensionObjects

def GetIndexTypeString():
    '''
    Centralized function to get the index type string for all our maps, in case we 
    change it in the future. Actually using uint16_t for now because I hope we never
    need to index more than 65535 extensions.
    '''
    return 'uint16_t'

# get list of current vulkan version names
def GetVersionList(tree):
    '''
    Returns list of version names from features that define API version macros.
    After vk.xml split features into base/graphics/compute, not all features have version macros.
    We extract VK_API_VERSION_X_Y from "API version macros" blocks and convert to VK_VERSION_X_Y
    format (which extension dependencies use).
    Args:
        tree: xml.etree.ElementTree object containing the vk.xml data
    Returns:
        list: version names like VK_VERSION_1_0, VK_VERSION_1_1, etc (NOT VK_API_VERSION_X_Y)
    '''
    result = []
    features = tree.findall('./feature')
    
    for feature in features:
        # Find "API version macros" require blocks
        for require in feature.findall('./require'):
            if require.get('comment') == 'API version macros':
                # Look for VK_API_VERSION_1_X types and convert to VK_VERSION_1_X
                for type_elem in require.findall('./type'):
                    type_name = type_elem.get('name')
                    # Only VK_API_VERSION_1_X macros (skip helper macros, VKSC, etc)
                    if type_name and type_name.startswith('VK_API_VERSION_1_') and 'VKSC' not in type_name:
                        # Convert VK_API_VERSION_1_1 -> VK_VERSION_1_1 (matches extension dep format)
                        version_name = type_name.replace('VK_API_VERSION_', 'VK_VERSION_')
                        result.append(version_name)
    
    return result

def GetVersionsGreaterThanOrEqualTo(versionNames, versionName):
    '''
    Returns a list of version names that are greater than or equal to the given version name.
    Because of how nicely formatted the version names are, we can just compare the strings.
    Args:
        versionNames: list of version names
        versionName: version name to compare against
    Returns:
        list: list of version names that are greater than or equal to the given version name
    '''
    result = []
    for version in versionNames:
        if version >= versionName:
            result.append(version)
    return result

def GetVersionsLessThan(versionNames, versionName):
    '''
    Returns a list of version names that are less than the given version name.
    Args:
        versionNames: list of version names
        versionName: version name to compare against
    Returns:
        list: list of version names that are less than the given version name
    '''
    result = []
    for version in versionNames:
        if version < versionName:
            result.append(version)
    return result

def MakeVersionStrVersionNumber(version):
    '''
    Converts version feature name to API version macro for C++ code generation.
    VK_VERSION_1_2 -> VK_API_VERSION_1_2
    Args:
        version: feature name like VK_VERSION_1_1
    Returns:
        string: API version macro like VK_API_VERSION_1_1
    '''
    if version.startswith('VK_VERSION_') and not version.startswith('VK_API_VERSION_'):
        return version.replace('VK_VERSION_', 'VK_API_VERSION_')
    return version

def VersionSortKey(item):
    '''
    Functor used to sort dicts that map version names, ensuring that "ANY_VERSION" comes first
    '''
    version_name = item[0]
    if version_name == "ANY_VERSION":
        return ""  # Empty string sorts before any other string
    return version_name

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

def FindAllPromotedExtensions(extensionObjects, versions):
    '''
    In Vulkan, extensions are occasionally either promoted to a new alias (for various
    reasons), or promoted to a new version of the API as they are made core to that version.
    We grab both cases here.
    Args:
        extensionObjects: list of ExtensionWithDependencies objects
        versions: list of version elements from vk.xml
    Returns:
        dict: mapping old extension names to new extension names
        dict: mapping version names to lists of extensions promoted to core in that version
        dict: mapping extension names to what they were obsoleted or deprecated by
    '''
    promotedExtensions = {}
    versionPromotedExtensions = {}
    for version in versions:
        versionPromotedExtensions[version] = []

    for extension in extensionObjects:
        if extension.promotedTo is not None and extension.promotedTo in versions:
            versionPromotedExtensions[extension.promotedTo].append(extension.name)
        elif extension.promotedTo is not None and extension.promotedTo not in versions:
            promotedExtensions[extension.name] = extension.promotedTo

    return promotedExtensions, versionPromotedExtensions

def FindAllDeprecatedExtensions(extensionObjects):
    '''
    Finds all extensions that are obsoleted or deprecated by either another extension, or
    a version of the API.
    Args:
        extensionObjects: list of ExtensionWithDependencies objects
    Returns:
        dict: mapping extension names to what they were obsoleted or deprecated by
    '''
    deprecatedExtensions = {}
    for extension in extensionObjects:
        if extension.obsoletedOrDeprecatedBy is not None:
            deprecatedExtensions[extension.name] = extension.obsoletedOrDeprecatedBy
    return deprecatedExtensions

def ParseDependencyString(dependencyString):
    '''
    Parses a dependency string into an AST
    '''
    parser = Lark(ExtensionDependencyGrammar)
    transformer = ExtensionDependencyTransformer()
    dependencyAST = parser.parse(dependencyString)
    return transformer.transform(dependencyAST)

def DependencyASTToDnf(node):
    '''
    Flattens a dependency AST into disjunctive normal form: a list of alternatives, any one of which satisfies the
    dependency. Each alternative is (frozenset of extension names, minimum version name).
    '''
    if node['type'] == 'VERSION':
        return [(frozenset(), node['name'])]
    elif node['type'] == 'EXTENSION':
        return [(frozenset([node['name']]), 'VK_VERSION_1_0')]
    elif node['type'] == 'OR':
        result = []
        for term in node['terms']:
            result.extend(DependencyASTToDnf(term))
        return result
    else:  # AND
        result = [(frozenset(), 'VK_VERSION_1_0')]
        for factor in node['factors']:
            result = [(leftExts | rightExts, max(leftVersion, rightVersion, key=VersionNameSortKey))
                      for (leftExts, leftVersion) in result
                      for (rightExts, rightVersion) in DependencyASTToDnf(factor)]
        return result

def VersionNameSortKey(versionName):
    '''
    Sort key for VK_VERSION_X_Y names, numeric so that a VK_VERSION_1_10 would sort after VK_VERSION_1_9
    '''
    major, minor = versionName.removeprefix('VK_VERSION_').split('_')
    return (int(major), int(minor))

def ProcessDependencyAST(extensionObject, dependencyAST, versionNameList):
    '''
    Processes a dependency AST for a given extension object, filling extensionObject.dependencies with one entry
    per version. Each entry lists the extensions to enable at that version, plus the version the chosen alternative
    requires when that is above 1.0. A version where no alternative is satisfiable lists only the lowest version
    any alternative needs, which the runtime lookup reports as "this API version does not support the extension".
    Args:
        extensionObject: ExtensionWithDependencies object to update
        dependencyAST: AST representation of the dependencies
        versionNameList: List of valid Vulkan version names
    '''
    # fixed: this used to walk the AST with a version stack that dropped version requirements, so a row could
    # read "for VK_VERSION_1_0 ... requires VK_VERSION_1_0" where vk.xml says the extension needs 1.1
    alternatives = list(dict.fromkeys(DependencyASTToDnf(dependencyAST[0])))
    extensionObject.dependencies = {}
    for version in versionNameList:
        satisfiable = [alt for alt in alternatives if VersionNameSortKey(alt[1]) <= VersionNameSortKey(version)]
        if len(satisfiable) == 0:
            lowestVersion = min((alt[1] for alt in alternatives), key=VersionNameSortKey)
            extensionObject.dependencies[version] = [lowestVersion]
            continue
        # the runtime can only enable one alternative, so take the one needing the fewest extensions
        extensions, requiredVersion = min(satisfiable, key=lambda alt: (len(alt[0]), VersionNameSortKey(alt[1]), sorted(alt[0])))
        dependencies = sorted(extensions)
        if requiredVersion != 'VK_VERSION_1_0':
            dependencies.append(requiredVersion)
        extensionObject.dependencies[version] = dependencies

def FinalizeDependencies(extensionObject, versionNameList):
    '''
    Marks the versions where the extension is core.
    '''

    # handle promotedTo case, by propagating promoted status to later versions than the promotedTo version
    if extensionObject.promotedTo is not None and extensionObject.promotedTo in versionNameList:
        # Get range of version names from promotedTo to the end of the list
        promotedToVersion = extensionObject.promotedTo
        promotedToVersionIndex = versionNameList.index(promotedToVersion)
        for version in versionNameList[promotedToVersionIndex:]:
            extensionObject.dependencies[version] = ["PromotedToCore"]
    
    # Empty dependency lists indicate that at that version, all we need is the extension itself. This
    # is different from being promoted, however.
    extensionObject.dependencies = dict(sorted(extensionObject.dependencies.items(), key=lambda item: VersionNameSortKey(item[0])))

def FindAllExtensionsDependencies(extensionObjects, versions):
    '''
    Find all dependencies required by an extension for a given version of the API. Updates
    extensionObjects in place with the dependencies found, listing them by version in the
    ExtensionDependencies objects.
    Args:
        extensionObjects: list of ExtensionWithDependencies objects
        versions: list of version macro names (VK_API_VERSION_X_Y)
    Returns:
        int: maximum overall number of dependencies found for any one extension
             (used to set the size of the std::array in generated header)
    '''

    mostDeps = 0
    mostTokens = []
    mostComplexExtension = None

    for extensionObject in extensionObjects:
        # If extension is aliased to another extension, we don't need to process it: we'll have deps for
        # the aliased extension instead.
        if extensionObject.aliasedTo is not None:
            continue

        # Check if the extension has dependencies
        dependenciesAttrib = extensionObject.xmlObject.get('depends')
        
        if dependenciesAttrib is not None:
            dependencyAST = ParseDependencyString(dependenciesAttrib)

            # Process the AST to update the extension's dependencies
            ProcessDependencyAST(extensionObject, dependencyAST.children, versions)
            FinalizeDependencies(extensionObject, versions)
            
            # Calculate total number of dependencies for this extension
            totalDeps = sum(len(deps) for deps in extensionObject.dependencies.values())
            if totalDeps > mostDeps:
                mostDeps = totalDeps
                mostComplexExtension = extensionObject
            


    print(f"Most dependencies: {mostDeps}")
    print(f"Most tokens: {mostTokens}")
    if mostComplexExtension is not None:
        print(f"Most complex extension: {mostComplexExtension.name}")
        print(f'Most complex extension dependencies: {mostComplexExtension.dependencies}')
    else:
        print("No complex extensions found")
    return mostDeps

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
    print('#include <cstdint>\n#include <array>\n#include <string_view>\n#include <cstddef>', file=fileStream)
    print('#include <limits>', file=fileStream)
    print('#include <vulkan/vulkan_core.h>\n', file=fileStream)

def WriteMasterExtensionNameTable(extensionObjects, versions, fileStream):
    '''
    Writes the master extension name table, containing the actual extension names we use elsewhere. 
    They're stored in a constant array so we don't ever have to construct new string objects when doing
    lookups
    Args:
        extensionObjects: list of ExtensionWithDependencies objects
        versions: list of version elements from vk.xml to get version names
        fileStream: file stream to write to
    '''
    nameTable = []

    extensionNamesList = [ext.name for ext in extensionObjects]

    # Unfortunately expensive list comprehension, but we need to exclude extensions that are aliased to other extensions ONLY
    # Not extensions that are aliased to versions, because that's totally fine
    unaliasedExtensionObjects = [ ext for ext in extensionObjects if ext.promotedTo is None or ext.promotedTo not in extensionNamesList ]
    for extension in unaliasedExtensionObjects:
        extension.nameIndex = len(nameTable)
        nameTable.append(extension.name)

    aliasedExtensionObjects = [ ext for ext in extensionObjects if ext.promotedTo is not None and ext.promotedTo not in versions ]
    for extension in aliasedExtensionObjects:
        # Find the extension object that this extension is aliased to
        aliasedExtension = next((e for e in extensionObjects if e.name == extension.promotedTo), None)
        if aliasedExtension is None:
            print(f"Warning: Aliased extension {extension.name} not found in extension objects")
        else:
            # Set name index because we still store aliased names in the master name table
            extension.nameIndex = len(nameTable)
            # Set index of the aliased extension to the index of the extension it's aliased to
            extension.aliasedTo = aliasedExtension.nameIndex
            nameTable.append(extension.name)
        
    print('constexpr static std::array<const char*, ' + str(len(nameTable)) + '> masterExtensionNameTable', file=fileStream)
    print('{', file=fileStream)

    for name in nameTable:
        print('    \"' + name + "\",", file=fileStream)

    print('};\n', file=fileStream)

def WriteExtensionIndexLookupTable(extensionObjects, fileStream):
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
    entries = []
    for extensionObject in extensionObjects:
        if extensionObject.nameIndex is None:
            continue
        targetIndex = (
            extensionObject.aliasedTo
            if extensionObject.aliasedTo is not None
            else extensionObject.nameIndex
        )
        entries.append((extensionObject.name, extensionObject.nameIndex, targetIndex))

    entries.sort(key=lambda x: x[0])

    print('struct ExtensionIndexEntry', file=fileStream)
    print('{', file=fileStream)
    print('    std::string_view Name;', file=fileStream)
    print('    size_t Index;', file=fileStream)
    print('};\n', file=fileStream)
    print(f'constexpr static std::array<ExtensionIndexEntry, {len(entries)}> extensionIndexLookupTable', file=fileStream)
    print('{{', file=fileStream)

    for extensionName, nameIndex, targetIndex in entries:
        print(f'    {{ masterExtensionNameTable[{nameIndex}], {targetIndex} }}, // {extensionName}', file=fileStream)

    print('}};\n', file=fileStream)


def WriteDeviceOrInstanceExtensionTable(extensionObjects, fileStream, extensionTypeToWrite):
    '''
    Writes out a table of extensions that are instance extensions.
    '''
    extensionIndices = []
    typeByIndex = {extensionObject.nameIndex: extensionObject.xmlObject.get('type') for extensionObject in extensionObjects}

    for extensionObject in extensionObjects:
        extensionType = extensionObject.xmlObject.get('type')
        correctExtensionType = extensionType == extensionTypeToWrite
        validNameIndex = extensionObject.nameIndex is not None
        isAliased = extensionObject.aliasedTo is not None
        if correctExtensionType and validNameIndex and not isAliased:
            extensionIndices.append(extensionObject.nameIndex)
        # the target of an alias can be of the other type: VK_EXT_debug_marker (device) was promoted to
        # VK_EXT_debug_utils (instance). Writing the target under the alias's type put VK_EXT_debug_utils in the
        # device table, and IsExtensionSupported then asked the physical device for it
        elif correctExtensionType and validNameIndex and isAliased and typeByIndex.get(extensionObject.aliasedTo) == extensionTypeToWrite:
            extensionIndices.append(extensionObject.aliasedTo)

    # ExtensionWrangler binary-searches this table, so it must be sorted and unique. Aliases append the index of
    # the extension they alias, which broke the order and made some device extensions (VK_KHR_dynamic_rendering)
    # read as neither instance nor device extensions
    extensionIndices = sorted(set(extensionIndices))

    print('// Table of ' + extensionTypeToWrite + ' extension indices, sorted, so that it can be binary-searched', file=fileStream)
    print('constexpr static std::array<size_t, ' + str(len(extensionIndices)) + '> ' + extensionTypeToWrite + 'ExtensionTable', file=fileStream)
    print('{', file=fileStream)
    for idx, extensionIdx in enumerate(extensionIndices):
        print('    ' + str(extensionIdx) + ',', file=fileStream)
    print('};\n', file=fileStream, flush=True)

def WriteExtensionDependencyTable(extensionObjects, versions, fileStream):
    '''
    Creates a two-level table that first maps versions to a dependency table for that version, and then within that maps each
    extension index to a list of dependencies. This way we can do a minimal amount of lookups to retrieve dependencies,
    but still keep the dependency table partitioned by version.
    '''
    # Built a local dict of extension name to extension indices for quick lookup
    extensionNameToIndexDict = {}
    for extensionObject in extensionObjects:
        if extensionObject.nameIndex is not None and extensionObject.aliasedTo is None:
            extensionNameToIndexDict[extensionObject.name] = extensionObject.nameIndex
        elif extensionObject.nameIndex is not None and extensionObject.aliasedTo is not None:
            extensionNameToIndexDict[extensionObject.name] = extensionObject.aliasedTo

    # Collect extensions by version
    anyVersionExtensions = {}  # extension_index -> dependencies
    versionedExtensions = {}   # version -> {extension_index -> dependencies}
    
    for extensionObject in extensionObjects:
        if extensionObject.nameIndex is None or extensionObject.aliasedTo is not None:
            continue
            
        extensionIndex = extensionObject.nameIndex
        hasVersionSpecificDeps = False
        
        if hasattr(extensionObject, 'dependencies') and extensionObject.dependencies:
            # Check if this extension has version-specific dependencies
            for version in versions:
                if version in extensionObject.dependencies:
                    hasVersionSpecificDeps = True
                    
                    if version not in versionedExtensions:
                        versionedExtensions[version] = {}
                    
                    # Get dependencies for this version
                    dependencies = extensionObject.dependencies[version]
                    dependencyIndices = []
                    
                    if len(dependencies) == 0:
                        # Empty dependency list means version-only dependency
                        dependencyIndices.append(version)
                    else:
                        for dependency in dependencies:
                            if dependency == "PromotedToCore":
                                # Extension was promoted to core in this version
                                dependencyIndices.append(version)
                                continue
                            if dependency.startswith('VK_VERSION_'):
                                # the alternative chosen for this version needs a core version above 1.0
                                dependencyIndices.append(dependency)
                                continue
                            if dependency in extensionNameToIndexDict:
                                dependencyIndices.append(extensionNameToIndexDict[dependency])
                    
                    versionedExtensions[version][extensionIndex] = dependencyIndices
        
        if not hasVersionSpecificDeps:
            # This extension has no version-specific dependencies
            dependencyIndices = []
            if hasattr(extensionObject, 'dependencies') and 'ANY_VERSION' in extensionObject.dependencies:
                dependencies = extensionObject.dependencies['ANY_VERSION']
                for dependency in dependencies:
                    if dependency == "PromotedToCore":
                        continue
                    if dependency in extensionNameToIndexDict:
                        dependencyIndices.append(extensionNameToIndexDict[dependency])
            
            anyVersionExtensions[extensionIndex] = dependencyIndices

    dependencyTargets = []
    dependencyEntries = []
    versionSlices = []

    # 18. Order version slices with 0 first if anyVersionExtensions is non-empty.
    if anyVersionExtensions:
        entryOffset = len(dependencyEntries)
        for extIndex in sorted(anyVersionExtensions.keys()):
            depIndices = anyVersionExtensions[extIndex]
            offset = len(dependencyTargets)
            count = len(depIndices)
            for dep in depIndices:
                depVal = MakeVersionStrVersionNumber(dep) if isinstance(dep, str) else str(dep)
                dependencyTargets.append(depVal)
            dependencyEntries.append((extIndex, offset, count))
        entryCount = len(dependencyEntries) - entryOffset
        versionSlices.append(('0', entryOffset, entryCount))

    # 19. Append each key present in versionedExtensions sorted by VersionNameSortKey ascending.
    for version in sorted(versionedExtensions.keys(), key=VersionNameSortKey):
        extMap = versionedExtensions[version]
        entryOffset = len(dependencyEntries)
        for extIndex in sorted(extMap.keys()):
            depIndices = extMap[extIndex]
            offset = len(dependencyTargets)
            count = len(depIndices)
            for dep in depIndices:
                depVal = MakeVersionStrVersionNumber(dep) if isinstance(dep, str) else str(dep)
                dependencyTargets.append(depVal)
            dependencyEntries.append((extIndex, offset, count))
        entryCount = len(dependencyEntries) - entryOffset
        versionSlices.append((MakeVersionStrVersionNumber(version), entryOffset, entryCount))

    print('struct ExtensionDependencyRange', file=fileStream)
    print('{', file=fileStream)
    print('    size_t ExtensionIndex;', file=fileStream)
    print('    uint32_t Offset;', file=fileStream)
    print('    uint32_t Count;', file=fileStream)
    print('};\n', file=fileStream)

    print('struct VersionDependencySlice', file=fileStream)
    print('{', file=fileStream)
    print('    uint32_t ApiVersion;', file=fileStream)
    print('    uint32_t EntryOffset;', file=fileStream)
    print('    uint32_t EntryCount;', file=fileStream)
    print('};\n', file=fileStream)

    print(f'constexpr static std::array<size_t, {len(dependencyTargets)}> extensionDependencyTargets', file=fileStream)
    print('{', file=fileStream)
    for target in dependencyTargets:
        print(f'    {target},', file=fileStream)
    print('};\n', file=fileStream)

    print(f'constexpr static std::array<ExtensionDependencyRange, {len(dependencyEntries)}> extensionDependencyEntries', file=fileStream)
    print('{{', file=fileStream)
    for extIndex, offset, count in dependencyEntries:
        print(f'    {{ {extIndex}, {offset}, {count} }},', file=fileStream)
    print('}};\n', file=fileStream)

    print(f'constexpr static std::array<VersionDependencySlice, {len(versionSlices)}> extensionDependencyTable', file=fileStream)
    print('{{', file=fileStream)
    for apiVersion, entryOffset, entryCount in versionSlices:
        print(f'    {{ {apiVersion}, {entryOffset}, {entryCount} }},', file=fileStream)
    print('}};\n', file=fileStream)


def PrintPromotedVersionedExtensions(promotedVersionedExtensions, extensionIdxDict, fileStream):
    def GetExtensionIdx(extension, extensionIdxDict):
        if extension in extensionIdxDict:
            return extensionIdxDict[extension]
        print(f"Warning: Extension {extension} not found in extension index dictionary")
        return 0

    promotedTargets = []
    promotedSlices = []

    sorted_items = sorted(promotedVersionedExtensions.items(), key=lambda item: VersionNameSortKey(item[0]))
    for version, versionedExtensions in sorted_items:
        if not versionedExtensions:
            continue
        indices = sorted(set(GetExtensionIdx(ext, extensionIdxDict) for ext in versionedExtensions))
        offset = len(promotedTargets)
        count = len(indices)
        promotedTargets.extend(indices)
        promotedSlices.append((MakeVersionStrVersionNumber(version), offset, count))

    print('struct PromotedVersionSlice', file=fileStream)
    print('{', file=fileStream)
    print('    uint32_t ApiVersion;', file=fileStream)
    print('    uint32_t Offset;', file=fileStream)
    print('    uint32_t Count;', file=fileStream)
    print('};\n', file=fileStream)

    print(f'constexpr static std::array<size_t, {len(promotedTargets)}> promotedExtensionIndices', file=fileStream)
    print('{', file=fileStream)
    for idx in promotedTargets:
        print(f'    {idx},', file=fileStream)
    print('};\n', file=fileStream)

    print(f'constexpr static std::array<PromotedVersionSlice, {len(promotedSlices)}> promotedExtensionsTable', file=fileStream)
    print('{{', file=fileStream)
    for apiVersion, offset, count in promotedSlices:
        print(f'    {{ {apiVersion}, {offset}, {count} }},', file=fileStream)
    print('}};\n', file=fileStream)


def WriteFeatureMemberTable(tree, headerText, outputPath):
    """
    Writes GeneratedFeatureTable.hpp: every VkBool32 member of every feature struct, and every VkBool32 or flags
    member of every property struct, with the struct's sType, size and member offset. Device creation uses it to set
    one member in a zeroed struct, and to read one member of a queried struct, by the names that Lodestone's
    capability table and the spine's feature needs use. A struct is written only when vulkan_core.h defines it, so
    platform and provisional structs stay out.
    """
    # names inside these blocks exist only when a build defines VK_ENABLE_BETA_EXTENSIONS, and ours does not
    headerText = re.sub(r'#ifdef VK_ENABLE_BETA_EXTENSIONS.*?#endif', '', headerText, flags=re.DOTALL)
    structNamePattern = re.compile(r'typedef struct (\w+) \{')
    definedStructs = set(structNamePattern.findall(headerText))

    aliases = {}
    structElements = {}

    for typeElement in tree.findall('./types/type[@category="struct"]'):
        name = typeElement.get('name')
        alias = typeElement.get('alias')

        if alias:
            aliases[name] = alias
            continue

        structElements[name] = typeElement

    # the versions and extensions that make each struct (or an alias of it) available
    requires = {}

    def addRequires(owner, element):
        for requiredType in element.findall('./require/type'):
            typeName = requiredType.get('name')
            canonical = aliases.get(typeName, typeName)
            requires.setdefault(canonical, [])

            if owner not in requires[canonical]:
                requires[canonical].append(owner)

    # vk.xml splits each version into internal blocks (VK_BASE_VERSION_1_2 and so on), so name each block by its
    # version number, as Lodestone's capability table does
    for featureElement in tree.findall('./feature'):
        if 'vulkan' in featureElement.get('api', 'vulkan').split(','):
            versionName = 'VK_VERSION_' + featureElement.get('number').replace('.', '_')
            addRequires(versionName, featureElement)

    for extensionElement in tree.findall('./extensions/extension'):
        if 'vulkan' in extensionElement.get('supported', '').split(','):
            addRequires(extensionElement.get('name'), extensionElement)

    def extendsOf(element):
        return (element.get('structextends') or '').split(',')

    featureStructs = []
    propertyStructs = []

    for name, element in sorted(structElements.items()):
        if name not in definedStructs:
            continue

        if 'VkPhysicalDeviceFeatures2' in extendsOf(element):
            featureStructs.append(name)
        elif 'VkPhysicalDeviceProperties2' in extendsOf(element):
            propertyStructs.append(name)

    def stypeOf(element):
        for member in element.findall('./member'):
            memberType = member.find('type')

            if memberType is not None and memberType.text == 'VkStructureType':
                return member.get('values')

        return None

    flagBitsTypes = {}

    for typeElement in tree.findall('./types/type[@category="bitmask"]'):
        nameElement = typeElement.find('name')
        bits = typeElement.get('requires') or typeElement.get('bitvalues')

        if nameElement is not None and bits:
            flagBitsTypes[nameElement.text] = bits

    def membersOf(structName, allowFlags):
        element = structElements[structName]
        members = []

        for member in element.findall('./member'):
            # vk.xml can list a member once per API (vulkan, vulkansc)
            if 'vulkan' not in member.get('api', 'vulkan').split(','):
                continue

            memberType = member.find('type')
            memberName = member.find('name')

            if memberType is None or memberName is None or member.find('enum') is not None:
                continue

            # an array member has [N] after its name, and is not one switch
            if memberName.tail and '[' in memberName.tail:
                continue

            if memberType.text == 'VkBool32':
                members.append((memberName.text, 'Bool32', None))
            elif allowFlags and memberType.text in flagBitsTypes:
                members.append((memberName.text, 'Flags32', flagBitsTypes[memberType.text]))

        return members

    rows = []
    structRows = []
    usedBitEnums = set()

    # the 1.0 features live inside VkPhysicalDeviceFeatures2, so their struct is VkPhysicalDeviceFeatures2
    for memberName, kind, _ in membersOf('VkPhysicalDeviceFeatures', False):
        rows.append(('VkPhysicalDeviceFeatures', memberName, kind,
                     f'offsetof(VkPhysicalDeviceFeatures2, features) + offsetof(VkPhysicalDeviceFeatures, {memberName})'))

    structRows.append(('VkPhysicalDeviceFeatures', 'StructKind::Feature', 'VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2',
                       'sizeof(VkPhysicalDeviceFeatures2)', 'VK_VERSION_1_0'))

    for kindName, structNames, allowFlags in (('StructKind::Feature', featureStructs, False),
                                              ('StructKind::Property', propertyStructs, True)):
        for structName in structNames:
            stype = stypeOf(structElements[structName])

            if stype is None:
                continue

            structRows.append((structName, kindName, stype, f'sizeof({structName})',
                               ','.join(requires.get(structName, []))))

            for memberName, kind, bitsType in membersOf(structName, allowFlags):
                rows.append((structName, memberName, kind, f'offsetof({structName}, {memberName})'))

                if bitsType:
                    usedBitEnums.add(bitsType)

    # the bits of every flags member: the enum's own values and the values that extensions add
    bitRows = []

    for enumsElement in tree.findall('./enums[@type="bitmask"]'):
        if enumsElement.get('name') in usedBitEnums:
            for enum in enumsElement.findall('./enum'):
                if enum.get('bitpos') is not None or enum.get('value') is not None:
                    bitRows.append(enum.get('name'))

    for extendingEnum in tree.findall('.//require/enum[@extends]'):
        if extendingEnum.get('extends') in usedBitEnums and extendingEnum.get('bitpos') is not None:
            bitRows.append(extendingEnum.get('name'))

    definedNames = set(re.findall(r'\b(VK_\w+)\s*=', headerText))
    bitRows = sorted(set(name for name in bitRows if name in definedNames))
    tableStructs = set(featureStructs) | set(propertyStructs)
    definedAliases = set(re.findall(r'typedef (\w+) (\w+);', headerText))
    aliasRows = sorted((alias, target) for alias, target in aliases.items()
                       if target in tableStructs and (target, alias) in definedAliases)

    rows.sort(key=lambda row: (row[0], row[1]))
    structRows.sort(key=lambda row: row[0])

    with open(outputPath, 'w', encoding='utf-8') as out:
        print('// Generated by GenerateExtensionAndFeatureHeaders.py from vk.xml. Do not edit.', file=out)
        print('#pragma once', file=out)
        print('#include <array>\n#include <cstddef>\n#include <cstdint>\n#include <string_view>\n#include <vulkan/vulkan_core.h>\n', file=out)
        print('namespace rhi::generated\n{', file=out)
        print('    enum class StructKind : uint8_t\n    {\n        Feature,\n        Property,\n    };\n', file=out)
        print('    enum class MemberKind : uint8_t\n    {\n        Bool32,\n        Flags32,\n    };\n', file=out)
        print('    struct StructInfo\n    {\n        std::string_view Name;\n        StructKind Kind;\n        VkStructureType SType;\n        uint32_t Size;\n        // the versions and extensions that make the struct available, separated by commas\n        std::string_view Requires;\n    };\n', file=out)
        print('    struct MemberInfo\n    {\n        std::string_view Struct;\n        std::string_view Member;\n        MemberKind Kind;\n        uint32_t Offset;\n    };\n', file=out)
        print('    struct StructAlias\n    {\n        std::string_view Alias;\n        std::string_view Name;\n    };\n', file=out)
        print('    struct FlagBit\n    {\n        std::string_view Name;\n        uint32_t Value;\n    };\n', file=out)

        print(f'    // sorted by Name\n    inline constexpr std::array<StructInfo, {len(structRows)}> k_Structs\n    {{ {{', file=out)
        for name, kind, stype, size, req in structRows:
            print(f'        {{ "{name}", {kind}, {stype}, static_cast<uint32_t>({size}), "{req}" }},', file=out)
        print('    } };\n', file=out)

        print(f'    // sorted by Struct, then Member\n    inline constexpr std::array<MemberInfo, {len(rows)}> k_Members\n    {{ {{', file=out)
        for struct, member, kind, offset in rows:
            print(f'        {{ "{struct}", "{member}", MemberKind::{kind}, static_cast<uint32_t>({offset}) }},', file=out)
        print('    } };\n', file=out)

        print(f'    // sorted by Alias\n    inline constexpr std::array<StructAlias, {len(aliasRows)}> k_StructAliases\n    {{ {{', file=out)
        for alias, target in aliasRows:
            print(f'        {{ "{alias}", "{target}" }},', file=out)
        print('    } };\n', file=out)

        print(f'    // sorted by Name\n    inline constexpr std::array<FlagBit, {len(bitRows)}> k_FlagBits\n    {{ {{', file=out)
        for name in bitRows:
            print(f'        {{ "{name}", static_cast<uint32_t>({name}) }},', file=out)
        print('    } };', file=out)
        print('}', file=out)

    print(f"Generated {len(structRows)} structs, {len(rows)} members, {len(aliasRows)} aliases, {len(bitRows)} flag bits")


def FinalizeFile(fileStream):
    '''
    Adds end of header guard and closes the file
    Args:
        fileStream: file stream to write to
    '''
    print('#endif // END_OF_HEADER', file=fileStream)
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
        
        # We peform this step even before we process the extensions to objects, as it's fairly involved and we 
        # don't even want to construct objects for empty extensions
        RemoveExtensionsWithZeroVersion(extensions)

        extensionObjects = ConstructExtensionObjects(extensions)

        # Find all aliased extensions, ones that have extensions that replace them with new names
        promotedAliasedExtensions, promotedVersionedExtensions = FindAllPromotedExtensions(extensionObjects, versions)
        print(f"Found {len(promotedAliasedExtensions)} extensions promoted to a new extension, and {len(promotedVersionedExtensions)} extensions promoted to a new version")
        deprecatedExtensions = FindAllDeprecatedExtensions(extensionObjects)
        print(f"Found {len(deprecatedExtensions)} deprecated extensions")
        mostDeps = FindAllExtensionsDependencies(extensionObjects, versions)

        
        outputPath = pathlib.Path(args.outputDir) / 'GeneratedExtensionHeader.hpp'
        print(f"Generating header file at {outputPath}")
        
        fileStream = open(outputPath, 'w', encoding='utf-8')
        CreateFileHeader(tree, fileStream)
        WriteMasterExtensionNameTable(extensionObjects, versions, fileStream)

        WriteExtensionIndexLookupTable(extensionObjects, fileStream)

        WriteDeviceOrInstanceExtensionTable(extensionObjects, fileStream, 'instance')
        WriteDeviceOrInstanceExtensionTable(extensionObjects, fileStream, 'device')


        WriteExtensionDependencyTable(extensionObjects, versions, fileStream)

        PrintPromotedVersionedExtensions(promotedVersionedExtensions, {ext.name: ext.nameIndex for ext in extensionObjects}, fileStream)

        FinalizeFile(fileStream)

        vulkanCorePath = specDirPath / 'Include/vulkan/vulkan_core.h'
        headerText = vulkanCorePath.read_text(encoding='utf-8')
        WriteFeatureMemberTable(tree, headerText, pathlib.Path(args.outputDir) / 'GeneratedFeatureTable.hpp')
        print("Successfully generated header file")
        
    except Exception as e:
        print(f"Error generating header: {e}")
        import traceback
        traceback.print_exc()
        exit(1)
