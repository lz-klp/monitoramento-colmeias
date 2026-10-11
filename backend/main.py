from datetime import datetime, timezone
from pathlib import Path
import sqlite3
from typing import Optional

from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel


# ============================================================
# CONFIGURAÇÃO
# ============================================================

APP_TITLE = "API - Monitoramento Inteligente de Colmeias"
DATABASE_PATH = Path(__file__).with_name("colmeias.db")


# ============================================================
# APLICAÇÃO FASTAPI
# ============================================================

app = FastAPI(
    title=APP_TITLE,
    version="0.1.0",
    description="Backend do MVP para receber e consultar dados das colmeias.",
)

# Para o MVP local, permite que uma interface web acesse a API.
# Em produção, substitua "*" pelos endereços permitidos.
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ============================================================
# MODELO DOS DADOS RECEBIDOS DO ESP32
# ============================================================

class MeasurementIn(BaseModel):
    device_id: str
    hive_id: str

    # Tempo desde que o ESP32 foi ligado.
    timestamp_ms: Optional[int] = None

    wifi_connected: Optional[bool] = None

    temperature_internal: Optional[float] = None
    humidity_internal: Optional[float] = None

    temperature_external: Optional[float] = None
    humidity_external: Optional[float] = None

    weight_kg: Optional[float] = None


# ============================================================
# BANCO SQLITE
# ============================================================

def get_connection() -> sqlite3.Connection:
    connection = sqlite3.connect(DATABASE_PATH)
    connection.row_factory = sqlite3.Row
    return connection


def create_database() -> None:
    with get_connection() as connection:
        connection.execute(
            """
            CREATE TABLE IF NOT EXISTS measurements (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                received_at TEXT NOT NULL,

                device_id TEXT NOT NULL,
                hive_id TEXT NOT NULL,

                timestamp_ms INTEGER,
                wifi_connected INTEGER,

                temperature_internal REAL,
                humidity_internal REAL,

                temperature_external REAL,
                humidity_external REAL,

                weight_kg REAL
            )
            """
        )
        connection.commit()


@app.on_event("startup")
def startup_event() -> None:
    create_database()


# ============================================================
# FUNÇÕES AUXILIARES
# ============================================================

def row_to_dict(row: sqlite3.Row) -> dict:
    data = dict(row)

    if data["wifi_connected"] is not None:
        data["wifi_connected"] = bool(data["wifi_connected"])

    return data


# ============================================================
# ROTAS
# ============================================================

@app.get("/")
def root() -> dict:
    return {
        "message": "API de monitoramento de colmeias funcionando.",
        "docs": "/docs",
        "health": "/health",
    }


@app.get("/health")
def health() -> dict:
    return {
        "status": "ok",
        "database": str(DATABASE_PATH.name),
    }


@app.post("/measurements", status_code=201)
def create_measurement(measurement: MeasurementIn) -> dict:
    received_at = datetime.now(timezone.utc).isoformat()

    with get_connection() as connection:
        cursor = connection.execute(
            """
            INSERT INTO measurements (
                received_at,
                device_id,
                hive_id,
                timestamp_ms,
                wifi_connected,
                temperature_internal,
                humidity_internal,
                temperature_external,
                humidity_external,
                weight_kg
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                received_at,
                measurement.device_id,
                measurement.hive_id,
                measurement.timestamp_ms,
                (
                    int(measurement.wifi_connected)
                    if measurement.wifi_connected is not None
                    else None
                ),
                measurement.temperature_internal,
                measurement.humidity_internal,
                measurement.temperature_external,
                measurement.humidity_external,
                measurement.weight_kg,
            ),
        )
        connection.commit()
        measurement_id = cursor.lastrowid

    return {
        "status": "created",
        "id": measurement_id,
        "received_at": received_at,
    }


@app.get("/measurements/latest")
def get_latest_measurement(
    hive_id: Optional[str] = Query(default=None),
) -> dict:
    with get_connection() as connection:
        if hive_id:
            row = connection.execute(
                """
                SELECT *
                FROM measurements
                WHERE hive_id = ?
                ORDER BY id DESC
                LIMIT 1
                """,
                (hive_id,),
            ).fetchone()
        else:
            row = connection.execute(
                """
                SELECT *
                FROM measurements
                ORDER BY id DESC
                LIMIT 1
                """
            ).fetchone()

    if row is None:
        raise HTTPException(
            status_code=404,
            detail="Nenhuma medição encontrada.",
        )

    return row_to_dict(row)


@app.get("/measurements")
def list_measurements(
    hive_id: Optional[str] = Query(default=None),
    limit: int = Query(default=100, ge=1, le=1000),
) -> list[dict]:
    with get_connection() as connection:
        if hive_id:
            rows = connection.execute(
                """
                SELECT *
                FROM measurements
                WHERE hive_id = ?
                ORDER BY id DESC
                LIMIT ?
                """,
                (hive_id, limit),
            ).fetchall()
        else:
            rows = connection.execute(
                """
                SELECT *
                FROM measurements
                ORDER BY id DESC
                LIMIT ?
                """,
                (limit,),
            ).fetchall()

    return [row_to_dict(row) for row in rows]
