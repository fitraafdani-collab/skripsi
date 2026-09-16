import pandas as pd
import numpy as np
import re
from sklearn.ensemble import RandomForestRegressor
from sklearn.impute import SimpleImputer
from sklearn.model_selection import train_test_split
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score

# 1. Load Data Excel
file_path = "Egg Candling Sheets Sync.xlsx"
df = pd.read_excel(file_path)

# Ekstrak umur telur dari kolom Egg ID
def extract_day(egg_id):
    match = re.search(r"hari\s*(\d+)", str(egg_id), re.IGNORECASE)
    return int(match.group(1)) if match else None

df["Day"] = df["Egg ID"].apply(extract_day)
df = df.dropna(subset=["Day"]).copy()

# 2. Fitur SAMA dengan train_model.py asli
features = [
    "Weight (g)", "Red", "Green", "Blue", "Clear", "Lux",
    "Color Temp (K)", "Norm R", "Norm G", "Norm B", "Confidence (%)"
]

X = df[features].copy()
y = df["Day"].copy()

# 3. Bagi data: 80% training, 20% testing
X_train, X_test, y_train, y_test = train_test_split(
    X, y, test_size=0.20, random_state=42
)

# 4. Imputasi: median dihitung dari training saja
imputer = SimpleImputer(strategy="median")
X_train_imputed = imputer.fit_transform(X_train)
X_test_imputed = imputer.transform(X_test)

# 5. Random Forest dengan parameter SAMA seperti model asli
rf = RandomForestRegressor(
    n_estimators=20,
    max_depth=8,
    random_state=42
)
rf.fit(X_train_imputed, y_train)

# 6. Prediksi data testing
y_pred = rf.predict(X_test_imputed)

# 7. Metrik
mae = mean_absolute_error(y_test, y_pred)
rmse = np.sqrt(mean_squared_error(y_test, y_pred))
r2 = r2_score(y_test, y_pred)

print("\n========================================")
print("HASIL EVALUASI RANDOM FOREST")
print("========================================")
print(f"Jumlah data   : {len(df)}")
print(f"Training      : {len(X_train)}")
print(f"Testing       : {len(X_test)}")
print(f"Jumlah tree   : 20")
print(f"Max depth     : 8")
print("----------------------------------------")
print(f"MAE           : {mae:.4f} hari")
print(f"RMSE          : {rmse:.4f} hari")
print(f"R²            : {r2:.4f}")
print(f"R² (%)        : {r2 * 100:.2f}%")
print("========================================\n")

# 8. Tabel prediksi
hasil = X_test.copy()
hasil.insert(0, "Hari Aktual", y_test.values)
hasil["Prediksi RF (hari)"] = y_pred
hasil["Error (hari)"] = hasil["Prediksi RF (hari)"] - hasil["Hari Aktual"]
hasil["Absolute Error (hari)"] = abs(hasil["Error (hari)"])
hasil["Squared Error (hari²)"] = hasil["Error (hari)"] ** 2

hasil = hasil.sort_values("Hari Aktual")

print("TABEL HASIL PREDIKSI DATA TESTING")
print("========================================")
print(hasil[[
    "Hari Aktual",
    "Prediksi RF (hari)",
    "Error (hari)",
    "Absolute Error (hari)",
    "Squared Error (hari²)"
]].to_string(index=False))

# 9. Simpan tabel hasil
output_file = "hasil_evaluasi_random_forest.xlsx"
hasil.to_excel(output_file, index=False)
print(f"\nFile hasil evaluasi dibuat: {output_file}")
