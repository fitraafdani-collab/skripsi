import pandas as pd
import numpy as np
import re
from sklearn.ensemble import RandomForestRegressor
from sklearn.impute import SimpleImputer

# 1. Load Data Excel
file_path = "Egg Candling Sheets Sync.xlsx"
df = pd.read_excel(file_path)

# Ekstrak umur telur dari kolom 'Egg ID'
def extract_day(egg_id):
    match = re.search(r'hari\s*(\d+)', str(egg_id), re.IGNORECASE)
    return int(match.group(1)) if match else None

df['Day'] = df['Egg ID'].apply(extract_day)

# Feature Columns
features = ['Weight (g)', 'Red', 'Green', 'Blue', 'Clear', 'Lux', 'Color Temp (K)', 'Norm R', 'Norm G', 'Norm B', 'Confidence (%)']
X = df[features].copy()
y = df['Day'].copy()

# Imputer
imputer = SimpleImputer(strategy='median')
X_imputed = imputer.fit_transform(X)

# 2. Train Model Random Forest Regressor
rf = RandomForestRegressor(n_estimators=20, max_depth=8, random_state=42)
rf.fit(X_imputed, y)

# 3. Generate Header C++ untuk Arduino IDE (C-Code Generation)
def tree_to_c(tree, feature_names):
    tree_ = tree.tree_
    feature_name = [
        feature_names[i] if i != -2 else "undefined!"
        for i in tree_.feature
    ]

    def recurse(node, depth):
        indent = "  " * depth
        if tree_.feature[node] != -2:
            name = feature_name[node]
            threshold = tree_.threshold[node]
            c_code = f"{indent}if (x[{features.index(name)}] <= {threshold:.6f}f) {{\n"
            c_code += recurse(tree_.children_left[node], depth + 1)
            c_code += f"{indent}}} else {{\n"
            c_code += recurse(tree_.children_right[node], depth + 1)
            c_code += f"{indent}}}\n"
            return c_code
        else:
            return f"{indent}return {tree_.value[node][0][0]:.6f}f;\n"

    return recurse(0, 1)

c_trees_code = ""
for i, estimator in enumerate(rf.estimators_):
    c_trees_code += f"static inline float predict_tree_{i}(const float* x) {{\n"
    c_trees_code += tree_to_c(estimator, features)
    c_trees_code += "}\n\n"

header_content = f"""#ifndef EGG_MODEL_H
#define EGG_MODEL_H

// Auto-generated Random Forest Model for ESP32
{c_trees_code}
static inline float predict_egg_age(const float* x) {{
    float sum = 0.0f;
"""

for i in range(len(rf.estimators_)):
    header_content += f"    sum += predict_tree_{i}(x);\n"

header_content += f"""    return sum / {len(rf.estimators_)}.0f;
}}

#endif // EGG_MODEL_H
"""

with open("egg_model.h", "w") as f:
    f.write(header_content)

print("Berhasil membuat file egg_model.h untuk Arduino IDE!")

