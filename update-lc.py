import os
import subprocess
import logging

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# Define the directory to start the search from
root_directory = './ToolKit'

# Define the file extensions to consider
file_extensions = ['.h', '.cpp']

# Define the existing and new license text
existing_license_text = '''/*
 * Copyright (c) 2019-2024 OtSofware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */
'''
new_license_text = '''/*
 * Copyright (c) 2019-2025 OtSoftware
 * This code is licensed under the GNU Lesser General Public License v3.0 (LGPL-3.0).
 * For more information, including options for a more permissive commercial license,
 * please visit [otyazilim.com] or contact us at [info@otyazilim.com].
 */
 
'''

# Recursively traverse through the directory structure
for root, dirs, files in os.walk(root_directory):
    for file in files:
        if file.endswith(tuple(file_extensions)):
            file_path = os.path.join(root, file)
            logger.info(f"Processing file: {file_path}")

            with open(file_path, 'r') as f:
                content = f.read()

            # Check if the file contains the existing license text
            if existing_license_text in content:
                logger.info(f"Existing license text found in {file_path}")
                # Replace existing license text with the new one
                modified_content = content.replace(existing_license_text, new_license_text)

                # Write the modified content back to the file
                with open(file_path, 'w') as f:
                    f.write(modified_content)

                logger.info(f"License text replaced in {file_path}")

                # Run clang-format to format the file
                subprocess.run(['clang-format', '-i', file_path])
                logger.info(f"File formatted: {file_path}")

logger.info("Script execution completed.")
