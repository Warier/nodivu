//! JSON limitado e estrito: rejeita duplicatas antes de construir um Value.
use crate::{ApiError, MAX_JSON_DEPTH, MAX_LINE_BYTES};
use serde::de::{self, DeserializeSeed, MapAccess, SeqAccess, Visitor};
use serde_json::{Map, Number, Value};
use std::fmt;

struct StrictValue {
    depth: usize,
}
impl<'de> DeserializeSeed<'de> for StrictValue {
    type Value = Value;
    fn deserialize<D: de::Deserializer<'de>>(self, d: D) -> Result<Value, D::Error> {
        d.deserialize_any(self)
    }
}
impl<'de> Visitor<'de> for StrictValue {
    type Value = Value;
    fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("JSON sem chaves duplicadas")
    }
    fn visit_bool<E: de::Error>(self, v: bool) -> Result<Value, E> {
        Ok(Value::Bool(v))
    }
    fn visit_i64<E: de::Error>(self, v: i64) -> Result<Value, E> {
        Ok(v.into())
    }
    fn visit_u64<E: de::Error>(self, v: u64) -> Result<Value, E> {
        Ok(v.into())
    }
    fn visit_f64<E: de::Error>(self, v: f64) -> Result<Value, E> {
        Number::from_f64(v)
            .map(Value::Number)
            .ok_or_else(|| E::custom("Número não finito"))
    }
    fn visit_str<E: de::Error>(self, v: &str) -> Result<Value, E> {
        Ok(v.into())
    }
    fn visit_string<E: de::Error>(self, v: String) -> Result<Value, E> {
        Ok(v.into())
    }
    fn visit_unit<E: de::Error>(self) -> Result<Value, E> {
        Ok(Value::Null)
    }
    fn visit_seq<A: SeqAccess<'de>>(self, mut seq: A) -> Result<Value, A::Error> {
        if self.depth >= MAX_JSON_DEPTH {
            return Err(de::Error::custom("Profundidade JSON excedida"));
        }
        let mut items = Vec::new();
        while let Some(v) = seq.next_element_seed(StrictValue {
            depth: self.depth + 1,
        })? {
            items.push(v);
        }
        Ok(Value::Array(items))
    }
    fn visit_map<A: MapAccess<'de>>(self, mut map: A) -> Result<Value, A::Error> {
        if self.depth >= MAX_JSON_DEPTH {
            return Err(de::Error::custom("Profundidade JSON excedida"));
        }
        let mut items = Map::new();
        while let Some(key) = map.next_key::<String>()? {
            if items.contains_key(&key) {
                return Err(de::Error::custom("Chave JSON duplicada"));
            }
            items.insert(
                key,
                map.next_value_seed(StrictValue {
                    depth: self.depth + 1,
                })?,
            );
        }
        Ok(Value::Object(items))
    }
}
pub fn parse(bytes: &[u8]) -> Result<Value, ApiError> {
    if bytes.len() > MAX_LINE_BYTES {
        return Err(ApiError::invalid("JSON excede limite de bytes."));
    }
    let mut d = serde_json::Deserializer::from_slice(bytes);
    let value = StrictValue { depth: 0 }
        .deserialize(&mut d)
        .map_err(|e| ApiError::invalid(e.to_string()))?;
    d.end().map_err(|e| ApiError::invalid(e.to_string()))?;
    Ok(value)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_duplicates_including_escaped_keys() {
        for s in [
            r#"{"a":1,"a":2}"#,
            r#"{"params":{"a":1,"\u0061":2}}"#,
            "{} {}",
            "[1e999]",
            "\"\u{fffd}",
        ] {
            assert!(parse(s.as_bytes()).is_err(), "{s}");
        }
        assert!(parse(&[0xff]).is_err());
    }
    #[test]
    fn enforces_container_depth() {
        for depth in [16, 17, 1000] {
            let s = format!("{}0{}", "[".repeat(depth), "]".repeat(depth));
            assert_eq!(parse(s.as_bytes()).is_ok(), depth == 16);
        }
    }
}
