import json
import time
import urllib.parse
import uuid

import boto3
from botocore.config import Config
from botocore.exceptions import ClientError

TABLE_NAME = "play_stats"
GSI_NAME = "gsi_updated_at"
RETRY_MAX_ATTEMPTS = 10
RETRY_MODE = "adaptive"
MAX_PAGE_SIZE = 200
dynamodb = boto3.client(
    "dynamodb",
    config=Config(
        retries={
            "max_attempts": RETRY_MAX_ATTEMPTS,
            "mode": RETRY_MODE,
        }
    ),
)


def _response(status_code: int, body: dict) -> dict:
    return {
        "statusCode": status_code,
        "headers": {
            "Content-Type": "application/json",
            "Access-Control-Allow-Origin": "*",
            "Access-Control-Allow-Headers": "content-type",
            "Access-Control-Allow-Methods": "OPTIONS,GET,POST",
        },
        "body": json.dumps(body),
    }


def _parse_body(event: dict) -> dict:
    body = event.get("body")
    if body is None:
        return {}
    if isinstance(body, dict):
        return body
    if isinstance(body, str) and body.strip():
        return json.loads(body)
    return {}


def _bad_request(error: str, details=None) -> dict:
    body = {"ok": False, "error": error}
    if details is not None:
        body["details"] = details
    return _response(400, body)


def _is_valid_uuid(value: str) -> bool:
    try:
        uuid.UUID(value)
        return True
    except ValueError:
        return False


def _dynamodb_error_response(exc: ClientError, op: str) -> dict:
    err = exc.response.get("Error", {})
    code = str(err.get("Code", "Unknown"))
    message = str(err.get("Message", str(exc)))
    throttle_codes = {
        "ProvisionedThroughputExceededException",
        "ThrottlingException",
        "RequestLimitExceeded",
    }
    if code in throttle_codes:
        return _response(
            429,
            {
                "ok": False,
                "error": f"DynamoDB {op} throttled",
                "error_code": code,
                "retryable": True,
                "details": message,
            },
        )
    return _response(
        500,
        {
            "ok": False,
            "error": f"DynamoDB {op} failed",
            "error_code": code,
            "retryable": False,
            "details": message,
        },
    )


def _parse_non_negative_int(raw_value, field_name: str):
    text = str(raw_value if raw_value is not None else "").strip()
    if not text:
        return 0, None
    try:
        value = int(text)
    except ValueError:
        return None, _bad_request(f"{field_name} must be integer")
    if value < 0:
        return None, _bad_request(f"{field_name} must be >= 0")
    return value, None


def _parse_limit(raw_value):
    text = str(raw_value if raw_value is not None else "").strip()
    if not text:
        return MAX_PAGE_SIZE, None
    try:
        limit = int(text)
    except ValueError:
        return None, _bad_request("limit must be integer")
    if limit <= 0:
        return None, _bad_request("limit must be > 0")
    if limit > MAX_PAGE_SIZE:
        return None, _bad_request(f"limit must be <= {MAX_PAGE_SIZE}")
    return limit, None


def _update_play_count(
    user_uuid: str, song_identity_key: str, delta: int, now: int
) -> dict:
    return dynamodb.update_item(
        TableName=TABLE_NAME,
        Key={
            "user_uuid": {"S": user_uuid},
            "song_identity_key": {"S": song_identity_key},
        },
        UpdateExpression=(
            "SET play_count = if_not_exists(play_count, :zero) + :delta, "
            "updated_at = :updated_at"
        ),
        ExpressionAttributeValues={
            ":zero": {"N": "0"},
            ":delta": {"N": str(delta)},
            ":updated_at": {"N": str(now)},
        },
        ReturnValues="ALL_NEW",
    )


def _decode_exclusive_start_key(last_evaluated_key: str) -> dict:
    decoded = json.loads(urllib.parse.unquote_plus(last_evaluated_key))
    eks = {
        "user_uuid": {"S": str(decoded.get("user_uuid", ""))},
        "updated_at": {"N": str(int(decoded.get("updated_at", 0)))},
    }
    song_identity_key = str(decoded.get("song_identity_key", ""))
    if song_identity_key:
        eks["song_identity_key"] = {"S": song_identity_key}
    return eks


def _encode_next_key(last_evaluated_key: dict) -> str:
    return urllib.parse.quote_plus(
        json.dumps(
            {
                "user_uuid": last_evaluated_key["user_uuid"]["S"],
                "updated_at": int(
                    last_evaluated_key.get("updated_at", {}).get("N", "0")
                ),
                "song_identity_key": last_evaluated_key.get(
                    "song_identity_key", {}
                ).get("S", ""),
            }
        )
    )


def _item_to_response(item: dict) -> dict:
    return {
        "song_identity_key": item["song_identity_key"]["S"],
        "play_count": int(item.get("play_count", {}).get("N", "0")),
        "updated_at": int(item.get("updated_at", {}).get("N", "0")),
    }


def _handle_get(event: dict) -> dict:
    query = event.get("queryStringParameters") or {}
    user_uuid = str(query.get("user_uuid", "")).strip()
    if not _is_valid_uuid(user_uuid):
        return _bad_request("Invalid user_uuid")

    updated_after, err = _parse_non_negative_int(
        query.get("updated_after"), "updated_after"
    )
    if err is not None:
        return err
    limit, err = _parse_limit(query.get("limit"))
    if err is not None:
        return err

    query_kwargs = {
        "TableName": TABLE_NAME,
        "IndexName": GSI_NAME,
        "ExpressionAttributeValues": {":u": {"S": user_uuid}},
        "ConsistentRead": False,
        "Limit": limit,
    }
    if updated_after > 0:
        query_kwargs["KeyConditionExpression"] = (
            "user_uuid = :u AND updated_at > :updated_after"
        )
        query_kwargs["ExpressionAttributeValues"][":updated_after"] = {
            "N": str(updated_after)
        }
    else:
        query_kwargs["KeyConditionExpression"] = "user_uuid = :u"

    last_evaluated_key = query.get("last_evaluated_key")
    if last_evaluated_key:
        try:
            query_kwargs["ExclusiveStartKey"] = _decode_exclusive_start_key(
                last_evaluated_key
            )
        except (json.JSONDecodeError, ValueError) as exc:
            return _bad_request("Invalid last_evaluated_key", str(exc))

    try:
        result = dynamodb.query(**query_kwargs)
    except ClientError as exc:
        return _dynamodb_error_response(exc, "query")

    items = [_item_to_response(item) for item in result.get("Items", [])]
    next_key = None
    if "LastEvaluatedKey" in result:
        next_key = _encode_next_key(result["LastEvaluatedKey"])

    return _response(
        200,
        {
            "ok": True,
            "user_uuid": user_uuid,
            "items": items,
            "next_last_evaluated_key": next_key,
        },
    )


def _validate_bulk_updates(updates):
    if not isinstance(updates, list):
        return None, _bad_request("updates must be array")
    if not updates:
        return None, _bad_request("updates cannot be empty")
    if len(updates) > MAX_PAGE_SIZE:
        return None, _bad_request(f"updates size must be <= {MAX_PAGE_SIZE}")

    merged = {}
    for i, item in enumerate(updates):
        if not isinstance(item, dict):
            return None, _bad_request(f"updates[{i}] must be object")
        key = str(item.get("song_identity_key", "")).strip()
        if not key:
            return None, _bad_request(f"updates[{i}].song_identity_key required")
        try:
            delta = int(item.get("delta", 1))
        except (ValueError, TypeError):
            return None, _bad_request(f"updates[{i}].delta must be integer")
        if delta <= 0:
            return None, _bad_request(f"updates[{i}].delta must be > 0")
        merged[key] = merged.get(key, 0) + delta
    return merged, None


def _handle_post(event: dict) -> dict:
    try:
        body = _parse_body(event)
    except json.JSONDecodeError:
        return _bad_request("Invalid JSON body")

    user_uuid = str(body.get("user_uuid", "")).strip()
    if not _is_valid_uuid(user_uuid):
        return _bad_request("Invalid user_uuid")

    now = int(time.time())

    if "updates" in body:
        merged, err = _validate_bulk_updates(body.get("updates"))
        if err is not None:
            return err

        applied = []
        try:
            for key, delta in merged.items():
                result = _update_play_count(user_uuid, key, delta, now)
                attrs = result.get("Attributes", {})
                applied.append(
                    {
                        "song_identity_key": key,
                        "delta": delta,
                        "play_count": int(attrs.get("play_count", {}).get("N", "0")),
                    }
                )
        except ClientError as exc:
            return _dynamodb_error_response(exc, "bulk update")

        return _response(
            200,
            {
                "ok": True,
                "user_uuid": user_uuid,
                "updated_at": now,
                "applied_count": len(applied),
                "applied": applied,
            },
        )

    song_identity_key = str(body.get("song_identity_key", "")).strip()
    try:
        delta = int(body.get("delta", 1))
    except (ValueError, TypeError):
        return _bad_request("delta must be integer")
    if not song_identity_key:
        return _bad_request("song_identity_key required")
    if delta <= 0:
        return _bad_request("delta must be > 0")

    try:
        result = _update_play_count(user_uuid, song_identity_key, delta, now)
    except ClientError as exc:
        return _dynamodb_error_response(exc, "update")

    attrs = result.get("Attributes", {})
    play_count = int(attrs.get("play_count", {}).get("N", "0"))

    return _response(
        200,
        {
            "ok": True,
            "user_uuid": user_uuid,
            "song_identity_key": song_identity_key,
            "play_count": play_count,
            "updated_at": now,
        },
    )


def handler(event, context):
    request_http = event.get("requestContext", {}).get("http", {})
    method = request_http.get("method", "")
    if method == "OPTIONS":
        return _response(200, {"ok": True})
    if method == "GET":
        return _handle_get(event)
    if method in ("POST", ""):
        return _handle_post(event)
    return _response(405, {"ok": False, "error": "Method not allowed"})
