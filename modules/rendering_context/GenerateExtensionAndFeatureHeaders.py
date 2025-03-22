#!/usr/bin/env python3
# Parse vk.xml to generate a header of available extensions for a platform, relevant queryable names,
# and generate a hidden-behind-source list of features of those extensions so that we can query those too
# Guidance from https://www.anteru.net/blog/2018/codegen-for-fast-vulkan/

from xml.etree import ElementTree
import argparse
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
    Constructs ExtensionWithDependencies objects from the list of extensions
    '''
    extensionObjects = []
    for extension in extensionXmlObjects:
        extensionObject = ExtensionWithDependencies()
        extensionObject.name = extension.get('name')
        extensionObject.nameIndex = None
        extensionObject.xmlObject = extension
        extensionObject.dependencies = {}
        extensionPlatform = extension.get('platform')
        if extensionPlatform is None:
            extensionPlatform = 'VULKAN_PLATFORM_ALL'
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

        result.append(version)

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
    versionNames = [version.get('name') for version in versions]
    for version in versionNames:
        versionPromotedExtensions[version] = []

    for extension in extensionObjects:
        if extension.promotedTo is not None and extension.promotedTo in versionNames:
            versionPromotedExtensions[extension.promotedTo].append(extension.name)
        elif extension.promotedTo is not None and extension.promotedTo not in versionNames:
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

def ProcessDependencyAST(extensionObject, dependencyAST, versionNameList):
    '''
    Processes a dependency AST for a given extension object, updating the extensionObject's dependencies
    with the dependencies found in the AST.
    Args:
        extensionObject: ExtensionWithDependencies object to update
        dependencyAST: AST representation of the dependencies
        versionNameList: List of valid Vulkan version names
    '''
    # Initialize the dependencies dictionary if it doesn't exist
    extensionObject.dependencies = {}

    # Keep stack of dependencies and versions we're currently processing,
    # pushing to the queue as we find new values and popping or applying 
    # the stack as we finish with the AND/OR nodes as appropriate.
    version_stack = []
    version_stack.append(versionNameList[0])
    extension_stack = []

    
    # Helper function to process OR nodes (alternative dependency paths)
    def process_or_node(node):
        nonlocal version_stack, extension_stack
        # Each OR node is always going to specify a version dependency, or an extension
        # dependency, OR another set of AST nodes to process.
        for term in node['terms']:
            type = process_node(term)
            if type == 'VERSION':
                # If an OR node processes a version, it indicates the end of the previous version's
                # dependency stack, and the beginning of a new version's dependency stack
                next_version_name = term['name']
                if next_version_name not in extensionObject.dependencies:
                    extensionObject.dependencies[next_version_name] = []
                extensionObject.dependencies[version_stack[-1]].extend(extension_stack)
                version_stack.append(next_version_name)
                extension_stack = []

        return 'OR'

    
    # Helper function to process AND nodes (dependencies that must all be satisfied)
    def process_and_node(node):
        # For AND nodes, all factors must be satisfied
        # We need to collect all dependencies and add them to the current version
        nonlocal version_stack, extension_stack
        
        for factor in node['factors']:
            type = process_node(factor)
            if type == 'VERSION':
                # If an AND node returns a version, it's going to be the first argument and always
                # indicates the beginning of support for an extension, so we pop the previous version 
                # (usually this is just vk1.0, as far as I can tell from investigating the vk.xml)
                version_stack.pop()
                version_stack.append(factor['name'])

        return 'AND'
    
    # Helper function to process leaf nodes (VERSION or EXTENSION)
    def process_leaf_node(node):
        nonlocal version_stack, extension_stack
        if node['type'] == 'VERSION':
            # For VERSION nodes, we create a version-specific dependency entry
            # We return the node type, because what we do with each version depends
            # if we're in an AND or OR node.
            version_name = node['name']
            if version_name not in extensionObject.dependencies:
                extensionObject.dependencies[version_name] = []
            return 'VERSION'
        elif node['type'] == 'EXTENSION':
            # For EXTENSION nodes, we add the extension to the current version's dependency list
            extension_name = node['name']
            extension_stack.append(extension_name)
            return 'EXTENSION'

    # Main recursive function to process any node type
    def process_node(node):
        nonlocal version_stack, extension_stack
        if node['type'] == 'OR':
            return process_or_node(node)
        elif node['type'] == 'AND':
            return process_and_node(node)
        else:  # VERSION or EXTENSION
            return process_leaf_node(node)
    
    # Start processing from the root node. Start with VK_VERSION_1_0 as leaves without versions
    # should end up there anyways, or will be processed during finalization.
    process_node(dependencyAST[0])
    if len(extension_stack) > 0:
        # Apply extensions in stack to versions from current version to end of available versions
        for version in versionNameList[versionNameList.index(version_stack[-1]):]:
            if version not in extensionObject.dependencies:
                extensionObject.dependencies[version] = []
            extensionObject.dependencies[version].extend(extension_stack)

def FinalizeDependencies(extensionObject, versionNameList):
    '''
    Finalizes the dependencies for an extension object, using the known version dependencies
    to forward fill and backward invalidate in the dependencies for versions as appropriate.
    '''

    if len(extensionObject.dependencies) == 1:
        # If there's only one version, we don't need to do anything
        return

    # First, we need to find the earliest version that has dependencies when the list has more than 
    # one version: this indicates the first version that supports the extension.
    firstVersionWithDependencies = None
    for version, dependencies in extensionObject.dependencies.items():
        if firstVersionWithDependencies is None and len(dependencies) > 0:
            firstVersionWithDependencies = version
            break

    # Starting with the first version with dependencies (which should now be the front of the dict), 
    # we need to iterate through dependency ranges (gaps between versions) and add dependencies from the previous
    # version to the next version if it's not already in the list.
    # Create a local copy to iterate over, because we're going to be modifying the dependencies list as we go
    extensionDependenciesCopy = extensionObject.dependencies.copy()
    for version, dependencies in extensionDependenciesCopy.items():
        found_current = False
        versionsToAdd = []
        for versionName in versionNameList:
            if found_current:
                # The key is that we're looking for the next version that hasn't had deps added yet
                # Add them to the list, and continue
                if versionName not in extensionObject.dependencies:
                    versionsToAdd.append(versionName)
                    continue
                # We found the next version with dependencies or which appeared in the AST,
                # ending our inclusive range of versions with dependencies to update
                elif versionName in extensionObject.dependencies:
                    break
            elif versionName == version:
                found_current = True
        
        # If there's no next version with dependencies, we're done with this version
        if len(versionsToAdd) == 0 or len(dependencies) == 0:
            continue
        else:
            # Add dependencies from the current version to the next version
            for versionToAdd in versionsToAdd:
                extensionObject.dependencies[versionToAdd] = dependencies
            print(f"Added dependencies ({dependencies}) for {extensionObject.name} from {version} to {versionsToAdd}")

    # handle promotedTo case, by propagating promoted status to later versions than the promotedTo version
    if extensionObject.promotedTo is not None and extensionObject.promotedTo in versionNameList:
        # Get range of version names from promotedTo to the end of the list
        promotedToVersion = extensionObject.promotedTo
        promotedToVersionIndex = versionNameList.index(promotedToVersion)
        for version in versionNameList[promotedToVersionIndex:]:
            extensionObject.dependencies[version] = ["PromotedToCore"]

    # Empty dependency lists now indicate that at that version, all we need is the extension itself. This
    # is different from being promoted, however.
    # Sort the dependencies list based on version name
    extensionObject.dependencies = dict(sorted(extensionObject.dependencies.items()))

def FindAllExtensionsDependencies(extensionObjects, versions):
    '''
    Find all dependencies required by an extension for a given version of the API. Updates
    extensionObjects in place with the dependencies found, listing them by version in the
    ExtensionDependencies objects.
    Args:
        extensionObjects: list of ExtensionWithDependencies objects
        versions: list of version elements from vk.xml
    Returns:
        int: maximum overall number of dependencies found for any one extension
             (used to set the size of the std::array in generated header)
    '''
    versionNameList = [version.get('name') for version in versions]

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
            # For debugging
            print('Dependencies in AST form for ' + extensionObject.name + ':')
            print(dependencyAST.children)

            if extensionObject.name == 'VK_KHR_video_decode_queue':
                print(versionNameList)
            
            # Process the AST to update the extension's dependencies
            ProcessDependencyAST(extensionObject, dependencyAST.children, versionNameList)
            FinalizeDependencies(extensionObject, versionNameList)
            print('Dependencies for ' + extensionObject.name + ':')
            print(extensionObject.dependencies)

    print(f"Most dependencies: {mostDeps}")
    print(f"Most tokens: {mostTokens}")
    print(f"Most complex extension: {mostComplexExtension.name}")
    print(f'Most complex extension dependencies: {mostComplexExtension.dependencies}')
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
    print('#include <cstdint>\n#include <array>\n#include <string_view>', file=fileStream)
    print('#include <unordered_map>\n#include <limits>', file=fileStream)
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
    versionNames = [version.get('name') for version in versions]

    extensionNamesList = [ext.name for ext in extensionObjects]

    # Unfortunately expensive list comprehension, but we need to exclude extensions that are aliased to other extensions ONLY
    # Not extensions that are aliased to versions, because that's totally fine
    unaliasedExtensionObjects = [ ext for ext in extensionObjects if ext.promotedTo is None or ext.promotedTo not in extensionNamesList ]
    for extension in unaliasedExtensionObjects:
        extension.nameIndex = len(nameTable)
        nameTable.append(extension.name)

    aliasedExtensionObjects = [ ext for ext in extensionObjects if ext.promotedTo is not None and ext.promotedTo not in versionNames ]
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
    print('static const std::unordered_map<std::string_view, size_t> extensionIndexLookupMap', file=fileStream)
    print('{', file=fileStream)

    for extensionObject in extensionObjects:
        extensionName = extensionObject.name
        if extensionObject.nameIndex is not None and extensionObject.aliasedTo is None:
            print(
                '    { masterExtensionNameTable[' + str(extensionObject.nameIndex) + '], ' +
                       str(extensionObject.nameIndex) + ' }, //' + extensionName, file=fileStream)
        elif extensionObject.nameIndex is not None and extensionObject.aliasedTo is not None:
            print('    // Aliased extension, remapped to: ' + extensionObject.promotedTo, file=fileStream)
            print(
                '    { masterExtensionNameTable[' + str(extensionObject.nameIndex) + '], ' +
                       str(extensionObject.aliasedTo) + ' }, //' + extensionName, file=fileStream)
        else:
            print(f"Warning: Extension {extensionName} not found in master name to index dictionary")

    print('};\n', file=fileStream)

def WriteExtensionHasVersionDependencyTable(versionedExtensionDependencies, extensionIdxDict, fileStream):
    '''
    Writes out a table of extensions that have a version dependency
    Args:
        versionedExtensionDependencies: dict mapping version names to a dict of extension names to lists of dependencies
        extensionIdxDict: mapping of extension names to their index in the masterExtensionNameTable
        fileStream: file stream to write to
    '''
    for version, extensionsAndDeps in versionedExtensionDependencies.items():
        for extensionName, dependencies in extensionsAndDeps.items():
            if "0x55555555" in dependencies:
                print(f"Extension {extensionName} has a version dependency")

def WriteDeviceOrInstanceExtensionTable(extensionObjects, fileStream, extensionTypeToWrite):
    '''
    Writes out a table of extensions that are instance extensions.
    '''
    extensionIndices = []
    for extensionObject in extensionObjects:
        extensionType = extensionObject.xmlObject.get('type')
        correctExtensionType = extensionType == extensionTypeToWrite
        validNameIndex = extensionObject.nameIndex is not None
        isAliased = extensionObject.aliasedTo is not None
        if correctExtensionType and validNameIndex and not isAliased:
            extensionIndices.append(extensionObject.nameIndex)
        elif correctExtensionType and validNameIndex and isAliased:
            extensionIndices.append(extensionObject.aliasedTo)

    print('// Table of ' + extensionTypeToWrite + ' extension indices, indexed by the extension name', file=fileStream)
    print('constexpr static std::array<size_t, ' + str(len(extensionIndices)) + '> ' + extensionTypeToWrite + 'ExtensionTable', file=fileStream)
    print('{', file=fileStream)
    for idx, extensionIdx in enumerate(extensionIndices):
        print('    ' + str(extensionIdx) + ',', file=fileStream)
    print('};\n', file=fileStream, flush=True)

def WriteExtensionDependencyTable(extensionObjects, versions, fileStream, maxDeps):
    '''
    Creates a two-level table that first maps versions to a dependency table for that version, and then within that maps each
    extension index to a list of dependencies. This way we can do a minimal amount of lookups to retrieve dependencies,
    but still keep the dependency table partitioned by version.

    I output the versioned dependency tables first, then the version index table. This allows me to have the version index table
    use references to reduce how messy the generated code looks, at least for now.

    Args:
        extensionObjects: list of ExtensionWithDependencies objects
        versions: list of version elements from vk.xml
        fileStream: file stream to write to
        maxDeps: maximum number of dependencies found for any one extension
    '''

    # Reduced to uint16_t for space savings and hopefully ease of hashing
    dependencyIndexType = 'uint16_t'
    invalidDepIdx = 'std::numeric_limits<' + dependencyIndexType + '>::max()'

    # Get list of macros for each version
    versionedDependencyTables = {}
    for version in versions:
        if version.get('name') == 'ANY_VERSION':
            continue
        versionName = version.get('name')
        versionMacro = MakeVersionStrVersionNumber(versionName)
        versionedDependencyTables[versionMacro] = {}
    

    # Built a local dict of extension name to extension indices for quick lookup
    extensionNameToIndexDict = {}
    for extensionObject in extensionObjects:
        if extensionObject.nameIndex is not None and extensionObject.aliasedTo is None:
            extensionNameToIndexDict[extensionObject.name] = extensionObject.nameIndex
        elif extensionObject.nameIndex is not None and extensionObject.aliasedTo is not None:
            extensionNameToIndexDict[extensionObject.name] = extensionObject.aliasedTo

    # We use a copy because we return to handle the ANY_VERSION case later, but remove extensions
    # as we go so that this case at least runs quicker
    extensionObjectsCopy = extensionObjects.copy()
    for extensionObject in extensionObjectsCopy:
        extensionName = extensionObject.name
        extensionDependencies = extensionObject.dependencies
        for version, dependencies in extensionDependencies.items():
            if version == "ANY_VERSION":
                continue

            versionMacro = MakeVersionStrVersionNumber(version)
            dependencyTable = versionedDependencyTables[versionMacro]
            dependencyTable[extensionObject.nameIndex] = []

            if len(dependencies) == 0:
                # Quick check: lets make sure promotedTo is the name of this version
                if extensionObject.promotedTo == version:
                    dependencyTable[extensionObject.nameIndex].append(versionMacro)
                else:
                    print(f"Warning: Extension {extensionObject.name} has a version dependency on {version}, but promotedTo is {extensionObject.promotedTo}")
            else:
                for dependency in dependencies:
                    dependencyIndex = extensionNameToIndexDict[dependency]
                    dependencyTable[extensionObject.nameIndex].append(dependencyIndex)

    for extensionObject in extensionObjectsCopy:
        noVersionDependencies = extensionObject.dependencies["ANY_VERSION"]
        for version, dependencies in extensionObject.dependencies.items():
            if version == "ANY_VERSION":
                continue
            for dependency in dependencies:
                dependencyIndex = extensionNameToIndexDict[dependency]
                versionedDependencyTables[version][extensionObject.nameIndex].append(dependencyIndex)



    # Now we can build the version index table
    versionIndexTable = {}



    # Build a list of VK_API_VERSION macros for each version in versions
    versionMacros = {}
    for version in versions:
        if version.get('name') == 'ANY_VERSION':
            continue
        versionName = version.get('name')
        versionMacros[versionName] = MakeVersionStrVersionNumber(versionName)

    dependencyVectorStr = 'std::array<size_t, ' + str(maxDeps) + '>'
    print('// Extension dependency table - partitioned by version, with \"ANY_VERSION\" extensions coming first followed by versioned extensions', file=fileStream)
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
            if version != "ANY_VERSION":
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

    # Sort and partition the dependencyIndexMap by version, with "ANY_VERSION" extensions coming first followed by versioned extensions
    # Generate a dict of version name to index of where the versioned extensions begin in the dependency table

    versionPartionedDependencyIndexMap = sorted(versionPartionedDependencyIndexMap.items(), key=VersionSortKey)

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
                # Add version name at the front of the dependency list if it's not ANY_VERSION
                dependencies = extensionsAndDeps[extensionName]
                if version != "ANY_VERSION":
                    dependencies = [f"{version}"] + dependencies
                dependencyNameString = ', '.join(dependencies)
            else:
                # If no dependencies, still show the version if applicable
                dependencyNameString = f"{version}" if version != "ANY_VERSION" else "None"
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
    print('static const std::unordered_map<uint32_t, size_t> versionedExtensionStartIndices', file=fileStream)
    print('{', file=fileStream)
    for version, index in versionedExtensionStartIndices.items():
        versionString = "    // Start of extensions for version: " + version
        print(versionString, file=fileStream)
        if version != "ANY_VERSION":
            print('    { ' + MakeVersionStrVersionNumber(version) + ', ' + str(index) + ' },', file=fileStream)
        else:
            print('    { 0, ' + str(index) + ' },', file=fileStream)
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
    Args:
        structs: list of feature structs
        extensions: list of extension elements from vk.xml
        versions: list of version elements from vk.xml
        struct_type: "Features" or "Properties"
    Returns:
        dict: mapping extension names to their feature struct
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
        extension_types = []
        if struct_type == "Features":
            extension_types = extension.findall('.//feature')
        elif struct_type == "Properties":
            extension_types = extension.findall('.//type')
        extension_feature_structs = []
        for extension_type in extension_types:
            extension_type_name = ''
            if struct_type == "Features":
                extension_type_name = extension_type.get('struct')
            elif struct_type == "Properties":
                extension_type_name = extension_type.get('name')
            # Check if this type is a feature struct by looking for struct_type in the name
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

def WriteExtensionFeatureStructsAndPointerTable(groupedExtensionFeatureStructs, fileStream):
    """
    Write out static instances of all the extension feature structs, with their sType
    filled out and pNext set to nullptr. Then, write out a table that maps extension indices
    to pointers to the structs.
    """
    extensionNameToStructName = {}
    for extensionName, featureStruct in groupedExtensionFeatureStructs.items():
        printedStructName = f'cowpoke_{extensionName}_FeatureStruct'
        extensionNameToStructName[extensionName] = printedStructName
        print(f'// Extension: {extensionName}', file=fileStream)
        print(f'static {featureStruct["name"]} {printedStructName} =\n{{', file=fileStream)
        print(f'    {featureStruct["type_enum"]},', file=fileStream)
        print(f'    nullptr,', file=fileStream)
        print(f'}};\n', file=fileStream)

def WriteExtensionPropertyStructsAndPointerTable(groupedExtensionPropertyStructs, fileStream):
    """
    Write out static instances of all the extension property structs, with their sType
    filled out and pNext set to nullptr. Then, write out a table that maps extension indices
    to pointers to the structs.
    """
    extensionNameToStructName = {}
    for extensionName, propertyStruct in groupedExtensionPropertyStructs.items():
        printedStructName = f'cowpoke_{extensionName}_PropertyStruct'
        extensionNameToStructName[extensionName] = printedStructName
        print(f'// Extension: {extensionName}', file=fileStream)
        print(f'static {propertyStruct["name"]} {printedStructName} =\n{{', file=fileStream)
        print(f'    {propertyStruct["type_enum"]},', file=fileStream)
        print(f'    nullptr,', file=fileStream)
        print(f'}};\n', file=fileStream)

def FinalizeFile(fileStream):
    '''
    Adds end of header guard and closes the file
    Args:
        fileStream: file stream to write to
    '''
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


        WriteExtensionDependencyTable(extensionObjects, versions, fileStream, mostDeps)

        # Pare down the list of extensions, to only include ones without the attribute "nofeatures=true". should save us some time
        extensions = [ext for ext in extensions if ext.get('nofeatures') != 'true']
        # Start querying and finding the structs, so we can print that table up
        featureStructs = FindFeatureStructs(tree)
        groupedFeatureStructs = GroupStructsByExtension(featureStructs, extensions, versions, "Features")
        WriteExtensionFeatureStructsAndPointerTable(groupedFeatureStructs, fileStream)


        propertyStructs = FindPropertyStructs(tree)
        groupedPropertyStructs = GroupStructsByExtension(propertyStructs, extensions, versions, "Properties")
        WriteExtensionPropertyStructsAndPointerTable(groupedPropertyStructs, fileStream)
        

        FinalizeFile(fileStream)
        print("Successfully generated header file")
        
    except Exception as e:
        print(f"Error generating header: {e}")
        import traceback
        traceback.print_exc()
        exit(1)
