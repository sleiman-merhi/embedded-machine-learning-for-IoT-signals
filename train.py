import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler, LabelEncoder
from sklearn.metrics import classification_report, confusion_matrix, accuracy_score
import joblib
import os

# ── 1. Configuration ──────────────────────────────────────────────────────────
DATA_DIR  = "."                      # folder containing real sensor CSVs
DATA_DIR1 = "./collected_data_1"     # folder containing additional CSVs
COLUMNS   = ["CO2", "Humidity", "Light", "Motion", "Smoke", "Temperature"]

# Real sensor collected CSVs
files = {
    "fire_alert":      "fire_alert.csv",
    "frost_risk":      "frost_risk.csv",
    "fungal_risk":     "fungal_risk.csv",
    "high_co2":        "high_co2.csv",
    "irrigation_need": "irrigation_need.csv",
    "night_motion":    "night_motion.csv",
    "normal":          "normal.csv",
    "shading":         "shadding.csv",
}

# Additional CSVs
files_1 = {
    "fire_alert":      "fire_alert_1.csv",
    "frost_risk":      "frost_risk_1.csv",
    "fungal_risk":     "fungal_risk_1.csv",
    "high_co2":        "high_co2_1.csv",
    "irrigation_need": "irrigation_need_1.csv",
    "night_motion":    "night_motion_1.csv",
    "normal":          "normal_1.csv",
    "shading":         "shadding_1.csv",
}

# ── 2. Load & label all CSVs ──────────────────────────────────────────────────
dfs = []

# Load real sensor CSVs
for label, fname in files.items():
    path = os.path.join(DATA_DIR, fname)
    df = pd.read_csv(path, header=None, names=COLUMNS, on_bad_lines="skip")
    df = df[pd.to_numeric(df["CO2"], errors="coerce").notna()]
    df = df.astype(float)
    df["label"] = label
    dfs.append(df)
    print(f"[REAL] {label:20s}: {len(df)} rows")

# Load additional CSVs
for label, fname in files_1.items():
    path = os.path.join(DATA_DIR1, fname)
    df = pd.read_csv(path, header=None, names=COLUMNS, on_bad_lines="skip")
    df = df[pd.to_numeric(df["CO2"], errors="coerce").notna()]
    df = df.astype(float)
    df["label"] = label
    dfs.append(df)
    print(f"[ADD]  {label:20s}: {len(df)} rows")

data = pd.concat(dfs, ignore_index=True)
print(f"\nTotal samples: {len(data)}")
print(f"Class distribution:\n{data['label'].value_counts()}\n")

# ── 3. Features & labels ──────────────────────────────────────────────────────
X = data[COLUMNS].values
y = data["label"].values

le = LabelEncoder()
y_enc = le.fit_transform(y)
print("Classes:", list(le.classes_))

# ── 4. Train/test split ───────────────────────────────────────────────────────
X_train, X_test, y_train, y_test = train_test_split(
    X, y_enc, test_size=0.2, random_state=42, stratify=y_enc
)
print(f"Train: {len(X_train)} | Test: {len(X_test)}")

# ── 5. Scale ──────────────────────────────────────────────────────────────────
scaler = StandardScaler()
X_train_sc = scaler.fit_transform(X_train)
X_test_sc  = scaler.transform(X_test)

print("\nStandardScaler means:", scaler.mean_)
print("StandardScaler stds: ", scaler.scale_)

# ── 6. Train Random Forest ────────────────────────────────────────────────────
print("\nTraining Random Forest...")
rf = RandomForestClassifier(n_estimators=100, random_state=42, n_jobs=-1)
rf.fit(X_train_sc, y_train)

# ── 7. Evaluate ───────────────────────────────────────────────────────────────
y_pred = rf.predict(X_test_sc)
acc = accuracy_score(y_test, y_pred)
print(f"\nTest Accuracy: {acc*100:.2f}%")
print("\nClassification Report:")
print(classification_report(y_test, y_pred, target_names=le.classes_))

# ── 8. Confusion Matrix ───────────────────────────────────────────────────────
cm = confusion_matrix(y_test, y_pred)
plt.figure(figsize=(10, 8))
sns.heatmap(cm, annot=True, fmt="d", cmap="Blues",
            xticklabels=le.classes_, yticklabels=le.classes_)
plt.title("Confusion Matrix — Random Forest")
plt.ylabel("True Label")
plt.xlabel("Predicted Label")
plt.xticks(rotation=45, ha="right")
plt.tight_layout()
plt.savefig("confusion_matrix.png", dpi=150)
plt.show()
print("Confusion matrix saved as confusion_matrix.png")

# ── 9. Feature Importance ─────────────────────────────────────────────────────
importances = rf.feature_importances_
plt.figure(figsize=(8, 5))
plt.bar(COLUMNS, importances, color="steelblue")
plt.title("Feature Importances — Random Forest")
plt.ylabel("Importance")
plt.tight_layout()
plt.savefig("feature_importance.png", dpi=150)
plt.show()
print("Feature importance saved as feature_importance.png")

# ── 10. Save model & scaler ───────────────────────────────────────────────────
joblib.dump(rf,     "rf_model.pkl")
joblib.dump(scaler, "scaler.pkl")
joblib.dump(le,     "label_encoder.pkl")
print("\nSaved: rf_model.pkl, scaler.pkl, label_encoder.pkl")
