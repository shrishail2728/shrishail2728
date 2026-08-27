from pathlib import Path


SOURCE = (Path(__file__).parents[1] / "src" / "main.cpp").read_text(
    encoding="utf-8"
)
SECRETS_EXAMPLE = (Path(__file__).parents[1] / "include" / "secrets.example.h")


def test_ds18b20_is_read_and_published():
    required_symbols = (
        "OneWire",
        "DallasTemperature",
        "DS18B20_PIN",
        "ds18b20Task",
        "requestTemperatures",
        "getTempCByIndex",
        "ThingSpeak.setField(6",
    )

    missing = [symbol for symbol in required_symbols if symbol not in SOURCE]
    assert not missing, f"DS18B20 integration is incomplete: {missing}"


def test_public_source_does_not_contain_credentials():
    assert 'const char *password = "' not in SOURCE
    assert 'const char *apiKey = "' not in SOURCE
    assert SECRETS_EXAMPLE.exists()


if __name__ == "__main__":
    test_ds18b20_is_read_and_published()
    test_public_source_does_not_contain_credentials()
    print("Firmware requirements: OK")
