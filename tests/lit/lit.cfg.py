# -*- Python -*-
"""Lit configuration for BlockType tests."""

import lit.formats
import os

# Configuration name
config.name = "BlockType"

# Test format: ShTest (shell-style tests)
config.test_format = lit.formats.ShTest(True)

# Test file suffixes
config.suffixes = ['.test']

# Test source root
config.test_source_root = os.path.dirname(__file__)

# Binary directory - use environment variable or default
binary_dir = getattr(config, 'binary_dir', None)
if not binary_dir:
    # Default to build-release in project root
    project_root = os.path.join(os.path.dirname(__file__), '..', '..')
    binary_dir = os.path.join(project_root, 'build-release')

# Test execution root
config.test_exec_root = os.path.join(binary_dir, 'tests', 'lit')

# Substitute blocktype binary path
blocktype_bin = os.path.join(binary_dir, 'tools', 'blocktype')
config.substitutions.append(('%blocktype', blocktype_bin))

# Substitute FileCheck
config.substitutions.append(('%FileCheck', 'FileCheck'))

# Substitute not
config.substitutions.append(('%not', 'not'))

# Features
config.available_features.add('blocktype')
