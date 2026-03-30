import os

files_to_patch = [
    "c:/ns-3/scratch/uhr-mld-mixed-pedca.cc",
    "c:/ns-3/run_comparison.sh"
]

for filepath in files_to_patch:
    with open(filepath, "r") as f:
        content = f.read()
    
    # Replace all instances of scratch/results with just results
    new_content = content.replace("scratch/results", "results")
    
    with open(filepath, "w") as f:
        f.write(new_content)

print("Paths patched successfully!")
