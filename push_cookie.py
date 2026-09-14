from __future__ import annotations

import asyncio
import inspect
import json
import os
import random
import re
import stat
import string
import subprocess
import sys
import tempfile
import time
from abc import ABC, abstractmethod
from datetime import datetime
from itertools import count
from pathlib import Path
from typing import Any, TypeAlias

import httpx
import pyotp
import requests

FB_AUTH_URL = "https://b-graph.facebook.com/auth/login"
REQUEST_TIMEOUT = 20
DIRECT_OTP_RE = re.compile(r"^\d{6,8}$")
TWO_FACTOR_SUBCODES = {1348162, 1348023}
DEFAULT_FB4A_API_KEY = "882a8490361da98702bf97a021ddc14d"
DEFAULT_FB4A_APP_ACCESS_TOKEN = "350685531728|62f8ce9f74b12f84c123cc23437a4a32"
LEGACY_FB4A_USER_AGENT = (
    "Dalvik/2.1.0 (Linux; U; Android 7.1.2; SM-G988N Build/NRD90M) "
    "[FBAN/FB4A;FBAV/340.0.0.27.113;FBPN/com.facebook.katana;FBLC/vi_VN;"
    "FBBV/324485361;FBCR/Viettel Mobile;FBMF/samsung;FBBD/samsung;"
    "FBDV/SM-G988N;FBSV/7.1.2;FBCA/x86:armeabi-v7a;"
    "FBDM/{density=1.0,width=540,height=960};FB_FW/1;FBRV/0;]"
)
_DOTENV_LOADED = False

DEFAULT_TIMEOUT = 60.0
TimeoutValue: TypeAlias = float | httpx.Timeout | None

_USER_AGENTS: list[str] = [
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36",
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36",
]
_SEC_CH_UA = '"Chromium";v="137", "Not;A=Brand";v="24", "Google Chrome";v="137"'
_REQUEST_COUNTER = count(1)

REQUIRED_SESSION_FIELDS: tuple[str, ...] = (
    "fb_dtsg",
    "jazoest",
    "sessionID",
    "FacebookID",
    "clientRevision",
)

_SPLIT_DATA_LIST: list[list[str]] = [
    ["fb_dtsg", 'DTSGInitialData",[],{"token":"', '"'],
    ["fb_dtsg_ag", 'async_get_token":"', '"'],
    ["jazoest", "jazoest=", '"'],
    ["hash", 'hash":"', '"'],
    ["sessionID", 'sessionId":"', '"'],
    ["FacebookID", '"actorID":"', '"'],
    ["clientRevision", 'client_revision":', ","],
]

HERE = Path(__file__).resolve().parent
CONFIG_PATH = HERE / "config.json"

EMAIL = "xxx"
PASSWORD = "xxxx"
OTP = None
ESP32_URL = "http://192.168.1.11/save"


def randStr(length: int) -> str:
    return "".join(random.choices(string.ascii_lowercase + string.digits, k=length))


def _load_dotenv_file_once() -> None:
    global _DOTENV_LOADED
    if _DOTENV_LOADED:
        return
    _DOTENV_LOADED = True
    env_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", ".env")
    )
    if not os.path.isfile(env_path):
        return
    try:
        with open(env_path, encoding="utf-8") as env_file:
            for raw_line in env_file:
                line = raw_line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                key, value = line.split("=", 1)
                key = key.strip()
                value = value.strip().strip('"').strip("'")
                if key and key not in os.environ:
                    os.environ[key] = value
    except OSError:
        return


def _get_config_value(*names: str, default: str = "") -> str:
    _load_dotenv_file_once()
    for name in names:
        value = os.environ.get(name)
        if value is not None and value.strip():
            return value.strip()
    return default


def jsonResults(
    dataJson: Any, statusLogin: int, listExportCookies: list[str] | None = None
) -> dict[str, Any]:
    payload = dataJson if isinstance(dataJson, dict) else {}
    cookies = listExportCookies or []
    if statusLogin == 1:
        return {
            "success": {
                "setCookies": "".join(cookies),
                "accessTokenFB": payload.get("access_token", ""),
                "cookiesKey-ValueList": payload.get("session_cookies", []),
            }
        }
    error_data = payload.get("error") or {}
    return {
        "error": {
            "title": error_data.get("error_user_title") or "Login failed",
            "description": error_data.get("error_user_msg") or "Unknown error",
            "error_subcode": error_data.get("error_subcode"),
            "error_code": error_data.get("code"),
            "fbtrace_id": error_data.get("fbtrace_id"),
        }
    }


def _build_cookie_export(session_cookies: Any) -> list[str]:
    exported: list[str] = []
    for cookie in session_cookies or []:
        name = cookie.get("name")
        value = cookie.get("value")
        if name is None or value is None:
            continue
        exported.append(f"{name}={value}; ")
    return exported


def _build_proxy(proxies: Any) -> str | None:
    if not proxies:
        return None
    value = str(proxies).strip()
    return value if "://" in value else f"http://{value}"


def _requests_proxy_config(proxies: Any) -> dict[str, str] | None:
    proxy = _build_proxy(proxies)
    return {"http": proxy, "https": proxy} if proxy else None


def _compact_response_text(text: Any, limit: int = 800) -> str:
    return " ".join(str(text or "").split())[:limit]


def _extract_error_payload(
    payload: Any, fallback_message: str, *, status_code: int | None = None
) -> dict[str, Any]:
    if isinstance(payload, dict):
        error = payload.get("error")
        if isinstance(error, dict):
            error.setdefault("code", status_code or error.get("code") or -1)
            error.setdefault("error_user_msg", fallback_message)
            return {"error": error}
        if payload.get("error_code") or payload.get("error_msg"):
            return {
                "error": {
                    "code": payload.get("error_code") or status_code or -1,
                    "error_subcode": payload.get("error_subcode"),
                    "error_user_title": payload.get("error_title"),
                    "error_user_msg": payload.get("error_msg") or fallback_message,
                    "fbtrace_id": payload.get("fbtrace_id"),
                }
            }
    return {
        "error": {
            "error_user_msg": fallback_message,
            "code": status_code or -1,
        }
    }


def _post_json(
    url: str, data: dict[str, Any], headers: dict[str, str], proxies: Any
) -> dict[str, Any]:
    try:
        response = requests.post(
            url,
            data=data,
            headers=headers,
            proxies=_requests_proxy_config(proxies),
            timeout=REQUEST_TIMEOUT,
        )
    except requests.RequestException as err:
        return {"error": {"error_user_msg": f"Network error: {err}", "code": -1}}

    try:
        payload = response.json()
    except ValueError:
        payload = None

    status_code = getattr(response, "status_code", 200)
    response_text = getattr(response, "text", "")
    if status_code >= 400:
        fallback = f"HTTP {status_code}: {_compact_response_text(response_text)}"
        return _extract_error_payload(payload, fallback, status_code=status_code)
    if payload is None:
        return {
            "error": {
                "error_user_msg": (
                    "Facebook trả response không phải JSON: "
                    f"{_compact_response_text(response_text)}"
                ),
                "code": -1,
            }
        }
    return payload


async def _post_json_async(
    url: str, data: dict[str, Any], headers: dict[str, str], proxies: Any
) -> dict[str, Any]:
    return await asyncio.to_thread(_post_json, url, data, headers, proxies)


def _error_result(
    title: str,
    description: str,
    *,
    error_code: int = -1,
    error_subcode: int | None = None,
) -> dict[str, Any]:
    return {
        "error": {
            "title": title,
            "description": description,
            "error_subcode": error_subcode,
            "error_code": error_code,
            "fbtrace_id": None,
        }
    }


def _validate_2fa_key(key2Fa: Any) -> tuple[str, bool]:
    if key2Fa is None:
        return "", False
    token = str(key2Fa).replace(" ", "").strip()
    if not token:
        return "", False
    if DIRECT_OTP_RE.fullmatch(token):
        return token, True
    return token, False


def _get_token_2fa_local(key2Fa: Any) -> str:
    try:
        token, is_direct = _validate_2fa_key(key2Fa)
        if not token or is_direct:
            return token
        normalized_secret = token.replace("-", "").upper()
        return pyotp.TOTP(normalized_secret).now()
    except (ValueError, TypeError) as exc:
        raise ValueError("2FA key không hợp lệ.") from exc


async def GetToken2FA(key2Fa: Any) -> str:
    return _get_token_2fa_local(key2Fa)


class loginFacebook:
    def __init__(
        self,
        username: str,
        password: str,
        AuthenticationGoogleCode: Any = None,
        proxies: Any = None,
    ) -> None:
        self.deviceID = self.adID = self.secureFamilyDeviceID = (
            f"{randStr(8)}-{randStr(4)}-{randStr(4)}-{randStr(4)}-{randStr(12)}"
        )
        self.manchineID = randStr(24)
        self.usernameFacebook = username
        self.passwordFacebook = password
        self.twoTokenAccess = AuthenticationGoogleCode
        self.proxies = proxies
        self.apiKey = _get_config_value("FBCHAT_API_KEY", default=DEFAULT_FB4A_API_KEY)
        self.appAccessToken = _get_config_value(
            "FBCHAT_APP_ACCESS_TOKEN",
            default=DEFAULT_FB4A_APP_ACCESS_TOKEN,
        )

    def _headers(self) -> dict[str, str]:
        return {
            "Host": "b-graph.facebook.com",
            "Content-Type": "application/x-www-form-urlencoded",
            "X-Fb-Connection-Type": "unknown",
            "User-Agent": LEGACY_FB4A_USER_AGENT,
            "X-Fb-Connection-Quality": "EXCELLENT",
            "Authorization": "OAuth null",
            "X-Fb-Friendly-Name": "authenticate",
            "Accept-Encoding": "gzip, deflate",
            "X-Fb-Server-Cluster": "True",
        }

    def _base_form(
        self, password: str, credentials_type: str, try_num: int
    ) -> dict[str, Any]:
        data: dict[str, Any] = {
            "adid": self.adID,
            "format": "json",
            "device_id": self.deviceID,
            "email": self.usernameFacebook,
            "password": password,
            "generate_analytics_claim": "1",
            "community_id": "",
            "cpl": "true",
            "try_num": str(try_num),
            "family_device_id": self.deviceID,
            "secure_family_device_id": self.secureFamilyDeviceID,
            "credentials_type": credentials_type,
            "fb4a_shared_phone_cpl_experiment": "fb4a_shared_phone_nonce_cpl_at_risk_v3",
            "fb4a_shared_phone_cpl_group": "enable_v3_at_risk",
            "enroll_misauth": "false",
            "generate_session_cookies": "1",
            "error_detail_type": "button_with_disabled",
            "source": "login",
            "machine_id": self.manchineID,
            "meta_inf_fbmeta": "",
            "advertiser_id": self.adID,
            "encrypted_msisdn": "",
            "currently_logged_in_userid": "0",
            "locale": "vi_VN",
            "client_country_code": "VN",
            "fb_api_req_friendly_name": "authenticate",
            "fb_api_caller_class": "Fb4aAuthHandler",
            "api_key": self.apiKey,
            "access_token": self.appAccessToken,
        }
        data["jazoest"] = "22421" if credentials_type == "password" else "22327"
        if credentials_type == "two_factor":
            data["sim_serials"] = "[]"
        return data

    def _login(self, data_form: dict[str, Any]) -> dict[str, Any]:
        return _post_json(FB_AUTH_URL, data_form, self._headers(), self.proxies)

    async def _login_async(self, data_form: dict[str, Any]) -> dict[str, Any]:
        return await _post_json_async(
            FB_AUTH_URL, data_form, self._headers(), self.proxies
        )

    def _extract_two_factor_metadata(self, error: dict[str, Any]) -> tuple[str, str]:
        error_data = error.get("error_data", {}) if isinstance(error, dict) else {}
        if isinstance(error_data, str):
            try:
                error_data = json.loads(error_data)
            except ValueError:
                error_data = {}
        user_id = str(
            error_data.get("uid")
            or error_data.get("userid")
            or error_data.get("user_id")
            or ""
        ).strip()
        first_factor = str(
            error_data.get("login_first_factor")
            or error_data.get("first_factor")
            or error_data.get("first_factor_id")
            or ""
        ).strip()
        return user_id, first_factor

    def _build_two_factor_form(
        self,
        token_2fa: str,
        user_id: str,
        first_factor: str,
        try_num: int,
        password_value: str | None = None,
    ) -> dict[str, Any]:
        data_form_2fa = self._base_form(
            password_value if password_value is not None else token_2fa,
            "two_factor",
            try_num,
        )
        data_form_2fa["twofactor_code"] = token_2fa
        data_form_2fa["userid"] = user_id
        data_form_2fa["first_factor"] = first_factor
        return data_form_2fa

    def _run_two_factor(
        self,
        token_2fa: str,
        user_id: str,
        first_factor: str,
        try_num: int,
        password_value: str | None = None,
    ) -> dict[str, Any]:
        return self._login(
            self._build_two_factor_form(
                token_2fa, user_id, first_factor, try_num, password_value
            )
        )

    async def _run_two_factor_async(
        self,
        token_2fa: str,
        user_id: str,
        first_factor: str,
        try_num: int,
        password_value: str | None = None,
    ) -> dict[str, Any]:
        return await self._login_async(
            self._build_two_factor_form(
                token_2fa, user_id, first_factor, try_num, password_value
            )
        )

    def main_blocking(self) -> dict[str, Any]:
        data_form = self._base_form(self.passwordFacebook, "password", 1)
        dataJson = self._login(data_form)
        error = dataJson.get("error")
        if error is None:
            return jsonResults(
                dataJson, 1, _build_cookie_export(dataJson.get("session_cookies"))
            )

        error_subcode = error.get("error_subcode")
        if error_subcode not in TWO_FACTOR_SUBCODES:
            return jsonResults(dataJson, 0)

        try:
            token_2fa = _get_token_2fa_local(self.twoTokenAccess)
        except ValueError as err:
            return _error_result("Invalid 2FA key", str(err), error_code=-2)

        if not token_2fa:
            return _error_result(
                "Missing 2FA token",
                "Facebook yêu cầu 2FA nhưng AuthenticationGoogleCode đang trống.",
                error_code=-2,
                error_subcode=error_subcode,
            )

        user_id, first_factor = self._extract_two_factor_metadata(error)
        if not user_id or not first_factor:
            return _error_result(
                "Missing 2FA metadata",
                "Facebook không trả về đủ `uid` hoặc `login_first_factor` để hoàn tất bước 2FA.",
                error_code=-3,
                error_subcode=error_subcode,
            )

        pass2Fa = self._run_two_factor(token_2fa, user_id, first_factor, 2)
        if pass2Fa.get("error") is not None:
            fallback_response = self._run_two_factor(
                token_2fa,
                user_id,
                first_factor,
                3,
                password_value=self.passwordFacebook,
            )
            if fallback_response.get("error") is None:
                return jsonResults(
                    fallback_response,
                    1,
                    _build_cookie_export(fallback_response.get("session_cookies")),
                )
        if pass2Fa.get("error") is not None:
            is_direct_otp = DIRECT_OTP_RE.fullmatch(
                str(self.twoTokenAccess or "").replace(" ", "").strip()
            )
            if not is_direct_otp:
                retry_token = _get_token_2fa_local(self.twoTokenAccess)
                if retry_token and retry_token != token_2fa:
                    retry_response = self._run_two_factor(
                        retry_token,
                        user_id,
                        first_factor,
                        4,
                    )
                    if retry_response.get("error") is None:
                        return jsonResults(
                            retry_response,
                            1,
                            _build_cookie_export(retry_response.get("session_cookies")),
                        )
            return jsonResults(pass2Fa, 0)

        return jsonResults(
            pass2Fa, 1, _build_cookie_export(pass2Fa.get("session_cookies"))
        )

    async def main(self) -> dict[str, Any]:
        data_form = self._base_form(self.passwordFacebook, "password", 1)
        dataJson = await self._login_async(data_form)
        error = dataJson.get("error")
        if error is None:
            return jsonResults(
                dataJson, 1, _build_cookie_export(dataJson.get("session_cookies"))
            )

        error_subcode = error.get("error_subcode")
        if error_subcode not in TWO_FACTOR_SUBCODES:
            return jsonResults(dataJson, 0)

        try:
            token_2fa = await GetToken2FA(self.twoTokenAccess)
        except ValueError as err:
            return _error_result("Invalid 2FA key", str(err), error_code=-2)

        if not token_2fa:
            return _error_result(
                "Missing 2FA token",
                "Facebook yêu cầu 2FA nhưng AuthenticationGoogleCode đang trống.",
                error_code=-2,
                error_subcode=error_subcode,
            )

        user_id, first_factor = self._extract_two_factor_metadata(error)
        if not user_id or not first_factor:
            return _error_result(
                "Missing 2FA metadata",
                "Facebook không trả về đủ `uid` hoặc `login_first_factor` để hoàn tất bước 2FA.",
                error_code=-3,
                error_subcode=error_subcode,
            )

        pass2Fa = await self._run_two_factor_async(token_2fa, user_id, first_factor, 2)
        if pass2Fa.get("error") is not None:
            fallback_response = await self._run_two_factor_async(
                token_2fa,
                user_id,
                first_factor,
                3,
                password_value=self.passwordFacebook,
            )
            if fallback_response.get("error") is None:
                return jsonResults(
                    fallback_response,
                    1,
                    _build_cookie_export(fallback_response.get("session_cookies")),
                )
        if pass2Fa.get("error") is not None:
            is_direct_otp = DIRECT_OTP_RE.fullmatch(
                str(self.twoTokenAccess or "").replace(" ", "").strip()
            )
            if not is_direct_otp:
                retry_token = await GetToken2FA(self.twoTokenAccess)
                if retry_token and retry_token != token_2fa:
                    retry_response = await self._run_two_factor_async(
                        retry_token,
                        user_id,
                        first_factor,
                        4,
                    )
                    if retry_response.get("error") is None:
                        return jsonResults(
                            retry_response,
                            1,
                            _build_cookie_export(retry_response.get("session_cookies")),
                        )
            return jsonResults(pass2Fa, 0)

        return jsonResults(
            pass2Fa, 1, _build_cookie_export(pass2Fa.get("session_cookies"))
        )


loginFB = loginFacebook


def _clean_kwargs(
    kwargs: dict[str, Any],
) -> tuple[str, bool, TimeoutValue, dict[str, Any]]:
    cleaned = dict(kwargs)
    url = str(cleaned.pop("url"))
    verify = bool(cleaned.pop("verify", True))
    timeout = cleaned.pop("timeout", DEFAULT_TIMEOUT)
    cleaned.pop("proxies", None)
    return url, verify, timeout, cleaned


def post_blocking(
    request_kwargs: dict[str, Any],
    *,
    client: httpx.Client | None = None,
) -> httpx.Response:
    url, verify, timeout, kwargs = _clean_kwargs(request_kwargs)
    if client is not None:
        return client.post(url, timeout=timeout, **kwargs)
    with httpx.Client(verify=verify, timeout=timeout) as owned_client:
        return owned_client.post(url, **kwargs)


def get_blocking(
    request_kwargs: dict[str, Any],
    *,
    client: httpx.Client | None = None,
) -> httpx.Response:
    url, verify, timeout, kwargs = _clean_kwargs(request_kwargs)
    if client is not None:
        return client.get(url, timeout=timeout, **kwargs)
    with httpx.Client(verify=verify, timeout=timeout) as owned_client:
        return owned_client.get(url, **kwargs)


async def post_async(
    request_kwargs: dict[str, Any],
    *,
    client: httpx.AsyncClient | None = None,
) -> httpx.Response:
    url, verify, timeout, kwargs = _clean_kwargs(request_kwargs)
    if client is not None:
        return await client.post(url, timeout=timeout, **kwargs)
    async with httpx.AsyncClient(verify=verify, timeout=timeout) as owned_client:
        return await owned_client.post(url, **kwargs)


async def get_async(
    request_kwargs: dict[str, Any],
    *,
    client: httpx.AsyncClient | None = None,
) -> httpx.Response:
    url, verify, timeout, kwargs = _clean_kwargs(request_kwargs)
    if client is not None:
        return await client.get(url, timeout=timeout, **kwargs)
    async with httpx.AsyncClient(verify=verify, timeout=timeout) as owned_client:
        return await owned_client.get(url, **kwargs)


def Headers(
    dataForm: dict[str, Any] | str | None = None, Host: str = "www.facebook.com"
) -> dict[str, str]:
    headers: dict[str, str] = {}
    headers["Host"] = Host
    headers["Connection"] = "keep-alive"
    headers["User-Agent"] = random.choice(_USER_AGENTS)
    headers["Accept"] = "*/*"
    headers["Origin"] = "https://" + Host
    headers["Sec-Fetch-Site"] = "same-origin"
    headers["Sec-Fetch-Mode"] = "cors"
    headers["Sec-Fetch-Dest"] = "empty"
    headers["Referer"] = "https://" + Host
    headers["sec-ch-ua"] = _SEC_CH_UA
    headers["sec-ch-ua-mobile"] = "?0"
    headers["sec-ch-ua-platform"] = '"Windows"'
    headers["Accept-Language"] = "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7"
    return headers


def digitToChar(digit: int) -> str:
    if digit < 10:
        return str(digit)
    return chr(ord("a") + digit - 10)


def str_base(number: int, base: int) -> str:
    if number < 0:
        return "-" + str_base(-number, base)
    d, m = divmod(number, base)
    if d > 0:
        return str_base(d, base) + digitToChar(m)
    return digitToChar(m)


def parse_cookie_string(cookie_str: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for part in cookie_str.split(";"):
        part = part.strip()
        if not part or "=" not in part:
            continue
        k, _, v = part.partition("=")
        out[k.strip()] = v.strip()
    return out


def dataSplit(
    string1: str,
    string2: str,
    numberSplit1: int | None = None,
    numberSplit2: int | None = None,
    HTML: str | None = None,
    amount: int | None = None,
    string3: str | None = None,
    numberSplit3: int | None = None,
    defaultValue: bool | None = None,
) -> str | None:
    if HTML is None:
        raise ValueError("HTML không được để trống.")
    if defaultValue:
        numberSplit1, numberSplit2 = 1, 0
    if numberSplit1 is None or numberSplit2 is None:
        raise ValueError("Thiếu chỉ số tách chuỗi.")
    if amount is None:
        return HTML.split(string1)[numberSplit1].split(string2)[numberSplit2]
    if amount == 3:
        if string3 is None or numberSplit3 is None:
            raise ValueError("Thiếu tham số cho lần tách chuỗi thứ ba.")
        return (
            HTML.split(string1)[numberSplit1]
            .split(string2)[numberSplit2]
            .split(string3)[numberSplit3]
        )
    raise ValueError(f"Số lần tách không được hỗ trợ: {amount}")


def formAll(
    dataFB: dict[str, Any],
    FBApiReqFriendlyName: str | None = None,
    docID: str | int | None = None,
    requireGraphql: bool = True,
) -> dict[str, Any]:
    dataForm: dict[str, Any] = {
        "fb_dtsg": dataFB["fb_dtsg"],
        "jazoest": dataFB["jazoest"],
        "__a": 1,
        "__user": str(dataFB["FacebookID"]),
        "__req": str_base(next(_REQUEST_COUNTER), 36),
        "__rev": dataFB["clientRevision"],
        "av": dataFB["FacebookID"],
    }
    if requireGraphql:
        dataForm["fb_api_caller_class"] = "RelayModern"
        dataForm["fb_api_req_friendly_name"] = FBApiReqFriendlyName
        dataForm["server_timestamps"] = "true"
        dataForm["doc_id"] = str(docID)
    return dataForm


def clearHTML(text: str) -> str:
    return re.compile(r"<[^>]+>").sub("", text)


def mainRequests(
    urlRequests: str, dataForm: dict[str, Any], setCookies: str
) -> dict[str, Any]:
    return {
        "headers": Headers(dataForm, "www.facebook.com"),
        "timeout": 60,
        "url": urlRequests,
        "data": dataForm,
        "cookies": parse_cookie_string(setCookies),
        "verify": True,
    }


def send_request(
    req_kwargs: dict[str, Any],
    *,
    client: httpx.Client | None = None,
) -> httpx.Response:
    return post_blocking(req_kwargs, client=client)


async def send_request_async(
    req_kwargs: dict[str, Any],
    *,
    client: httpx.AsyncClient | None = None,
) -> httpx.Response:
    return await post_async(req_kwargs, client=client)


def send_get_request(
    req_kwargs: dict[str, Any],
    *,
    client: httpx.Client | None = None,
) -> httpx.Response:
    return get_blocking(req_kwargs, client=client)


async def send_get_request_async(
    req_kwargs: dict[str, Any],
    *,
    client: httpx.AsyncClient | None = None,
) -> httpx.Response:
    return await get_async(req_kwargs, client=client)


def parse_json_response(
    text: str, *, strip_for_loop_prefix: bool = False
) -> dict[str, Any]:
    payload = (text or "").strip()
    if strip_for_loop_prefix and payload.startswith("for (;;);"):
        payload = payload[len("for (;;);") :].lstrip()
    parsed = json.loads(payload)
    if not isinstance(parsed, dict):
        raise ValueError("Facebook trả về JSON không phải object.")
    return parsed


def post_form_json(
    url: str,
    data_form: dict[str, Any],
    cookies: str,
    *,
    strip_for_loop_prefix: bool = False,
    client: httpx.Client | None = None,
) -> dict[str, Any]:
    response = send_request(mainRequests(url, data_form, cookies), client=client)
    response.raise_for_status()
    return parse_json_response(
        response.text, strip_for_loop_prefix=strip_for_loop_prefix
    )


async def post_form_json_async(
    url: str,
    data_form: dict[str, Any],
    cookies: str,
    *,
    strip_for_loop_prefix: bool = False,
    client: httpx.AsyncClient | None = None,
) -> dict[str, Any]:
    response = await send_request_async(
        mainRequests(url, data_form, cookies),
        client=client,
    )
    response.raise_for_status()
    return parse_json_response(
        response.text, strip_for_loop_prefix=strip_for_loop_prefix
    )


def generate_session_id() -> int:
    return random.randint(1, 2**53)


def generate_client_id() -> str:
    def gen(length: int) -> str:
        return "".join(
            random.choices(string.ascii_lowercase + string.digits, k=length)
        )

    return gen(8) + "-" + gen(4) + "-" + gen(4) + "-" + gen(4) + "-" + gen(12)


def json_minimal(data: Any) -> str:
    return json.dumps(data, separators=(",", ":"))


def gen_threading_id() -> str:
    return str(
        int(
            format(int(time.time() * 1000), "b")
            + (
                "0000000000000000000000"
                + format(int(random.random() * 4294967295), "b")
            )[-22:],
            2,
        )
    )


def require_list(list_: list[Any] | Any) -> set[Any]:
    if isinstance(list_, list):
        return set(list_)
    return set([list_])


def formatResults(type: str, text: str) -> dict[str, str]:
    return {"status": type, "message": text}


class SessionStorage(ABC):
    @abstractmethod
    def load(self) -> str | None: ...

    @abstractmethod
    def save(self, cookies: str) -> None: ...

    @abstractmethod
    def clear(self) -> None: ...


class FileSessionStorage(SessionStorage):
    def __init__(self, filepath: str = "config.json", key: str = "cookies") -> None:
        self.filepath = Path(filepath)
        self.key = key

    def _read_data(self) -> dict[str, Any] | None:
        if not self.filepath.exists():
            return None
        try:
            with self.filepath.open("r", encoding="utf-8") as file_handle:
                data = json.load(file_handle)
        except (json.JSONDecodeError, OSError):
            return None
        return data if isinstance(data, dict) else None

    def _write_data(self, data: dict[str, Any]) -> None:
        self.filepath.parent.mkdir(parents=True, exist_ok=True)
        temporary_path: Path | None = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="w",
                encoding="utf-8",
                dir=self.filepath.parent,
                prefix=f".{self.filepath.name}.",
                suffix=".tmp",
                delete=False,
            ) as file_handle:
                temporary_path = Path(file_handle.name)
                json.dump(data, file_handle, indent=2, ensure_ascii=False)
                file_handle.write("\n")
                file_handle.flush()
                os.fsync(file_handle.fileno())
            if os.name != "nt":
                temporary_path.chmod(0o600)
            os.replace(temporary_path, self.filepath)
        finally:
            if temporary_path is not None and temporary_path.exists():
                temporary_path.unlink(missing_ok=True)

    def load(self) -> str | None:
        data = self._read_data()
        if data is None:
            return None
        value = data.get(self.key)
        return value if isinstance(value, str) and value else None

    def save(self, cookies: str) -> None:
        if not isinstance(cookies, str) or not cookies.strip():
            raise ValueError("Cookie lưu vào storage phải là chuỗi không rỗng.")
        data = self._read_data() or {}
        data[self.key] = cookies
        self._write_data(data)

    def clear(self) -> None:
        data = self._read_data()
        if data is None or self.key not in data:
            return
        del data[self.key]
        self._write_data(data)


class EnvSessionStorage(SessionStorage):
    def __init__(self, env_var: str = "FB_COOKIES") -> None:
        self.env_var = env_var

    def load(self) -> str | None:
        return os.environ.get(self.env_var)

    def save(self, cookies: str) -> None:
        if not isinstance(cookies, str) or not cookies.strip():
            raise ValueError("Cookie lưu vào storage phải là chuỗi không rỗng.")
        os.environ[self.env_var] = cookies

    def clear(self) -> None:
        os.environ.pop(self.env_var, None)


def set_private_file_permissions(path: Path) -> None:
    if os.name != "nt":
        os.chmod(path, stat.S_IRUSR | stat.S_IWUSR)
        return

    acl_env = os.environ.copy()
    acl_env["FBCHAT_PRIVATE_FILE"] = str(path.resolve())
    acl_script = r"""
$ErrorActionPreference = 'Stop'
$targetPath = $env:FBCHAT_PRIVATE_FILE
$currentSid = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
$systemSid = New-Object System.Security.Principal.SecurityIdentifier('S-1-5-18')
$acl = Get-Acl -LiteralPath $targetPath
$allowed = @($currentSid.Value, $systemSid.Value)
$present = @{}
$isPrivate = $acl.AreAccessRulesProtected
foreach ($rule in $acl.Access) {
    $sid = $rule.IdentityReference.Translate(
        [System.Security.Principal.SecurityIdentifier]
    ).Value
    $present[$sid] = $true
    $hasFullControl = (($rule.FileSystemRights -band
        [System.Security.AccessControl.FileSystemRights]::FullControl) -eq
        [System.Security.AccessControl.FileSystemRights]::FullControl)
    if (($allowed -notcontains $sid) -or
        ($rule.AccessControlType -ne
            [System.Security.AccessControl.AccessControlType]::Allow) -or
        (-not $hasFullControl)) {
        $isPrivate = $false
    }
}
foreach ($sid in $allowed) {
    if (-not $present.ContainsKey($sid)) {
        $isPrivate = $false
    }
}
if ($isPrivate) {
    exit 0
}

$acl.SetAccessRuleProtection($true, $false)
foreach ($identity in @(
    $acl.Access |
        ForEach-Object { $_.IdentityReference } |
        Sort-Object Value -Unique
)) {
    $acl.PurgeAccessRules($identity)
}
foreach ($sid in @($currentSid, $systemSid)) {
    $rule = New-Object System.Security.AccessControl.FileSystemAccessRule(
        $sid,
        [System.Security.AccessControl.FileSystemRights]::FullControl,
        [System.Security.AccessControl.AccessControlType]::Allow
    )
    [void]$acl.AddAccessRule($rule)
}
Set-Acl -LiteralPath $targetPath -AclObject $acl

$verified = Get-Acl -LiteralPath $targetPath
if (-not $verified.AreAccessRulesProtected) {
    throw 'ACL vẫn còn kế thừa.'
}
foreach ($rule in $verified.Access) {
    $sid = $rule.IdentityReference.Translate(
        [System.Security.Principal.SecurityIdentifier]
    ).Value
    if (($allowed -notcontains $sid) -or
        ($rule.AccessControlType -ne
            [System.Security.AccessControl.AccessControlType]::Allow)) {
        throw "ACL còn principal không được phép: $sid"
    }
}
"""
    result = subprocess.run(
        [
            "powershell.exe",
            "-NoLogo",
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            acl_script,
        ],
        capture_output=True,
        text=True,
        timeout=10,
        check=False,
        env=acl_env,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise PermissionError(
            f"Không thể giới hạn ACL cho {path}: {detail or 'Set-Acl thất bại'}"
        )


def _has_value(value: Any) -> bool:
    return value is not None and str(value).strip() != ""


def _build_home_request(setCookies: str) -> dict[str, Any]:
    return {
        "headers": {
            "authority": "www.facebook.com",
            "method": "GET",
            "path": "/",
            "scheme": "https",
            "accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7",
            "accept-language": "vi-VN,vi;q=0.9,fr-FR;q=0.8,fr;q=0.7,en-US;q=0.6,en;q=0.5",
            "cache-control": "max-age=0",
            "cookie": setCookies,
            "dpr": "1.25",
            "priority": "u=0, i",
            "sec-ch-prefers-color-scheme": "dark",
            "sec-ch-ua": '"Chromium";v="140", "Not=A?Brand";v="24", "Google Chrome";v="140"',
            "sec-ch-ua-full-version-list": '"Chromium";v="140.0.7339.128", "Not=A?Brand";v="24.0.0.0", "Google Chrome";v="140.0.7339.128"',
            "sec-ch-ua-mobile": "?0",
            "sec-ch-ua-model": '""',
            "sec-ch-ua-platform": '"Windows"',
            "sec-ch-ua-platform-version": '"19.0.0"',
            "sec-fetch-dest": "document",
            "sec-fetch-mode": "navigate",
            "sec-fetch-site": "same-origin",
            "sec-fetch-user": "?1",
            "upgrade-insecure-requests": "1",
            "user-agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0.0.0 Safari/537.36",
            "viewport-width": "493",
        },
        "timeout": 30,
        "url": "https://www.facebook.com/",
        "cookies": parse_cookie_string(setCookies),
        "verify": True,
    }


def _parse_home_response(html: str, setCookies: str) -> dict[str, Any] | None:
    dictValueSaved: dict[str, Any] = {}
    for i in _SPLIT_DATA_LIST:
        nameValue = i[0]
        try:
            exportValue = dataSplit(i[1], i[2], HTML=html, defaultValue=True)
        except (IndexError, AttributeError, TypeError):
            exportValue = None
        dictValueSaved[nameValue] = exportValue
    dictValueSaved["cookieFacebook"] = setCookies

    missing = [
        field
        for field in REQUIRED_SESSION_FIELDS
        if not _has_value(dictValueSaved.get(field))
    ]
    facebook_id = str(dictValueSaved.get("FacebookID") or "").strip()
    if facebook_id and not facebook_id.isdigit():
        missing.append("FacebookID")

    if missing:
        missing_fields = ", ".join(dict.fromkeys(missing))
        print(f"[session] Thiếu hoặc sai token bắt buộc: {missing_fields}")
        return None

    return dictValueSaved


def _resolve_cookies(
    setCookies: str | None, storage: SessionStorage | None
) -> str | None:
    if setCookies is None and storage is not None:
        setCookies = storage.load()
    return setCookies


def _dataGetHome_blocking(
    setCookies: str | None = None, storage: SessionStorage | None = None
) -> dict[str, Any] | None:
    setCookies = _resolve_cookies(setCookies, storage)
    if not setCookies:
        print("[session] Không có cookie để khởi tạo session.")
        return None

    try:
        response = send_get_request(_build_home_request(setCookies))
        response.raise_for_status()
    except httpx.RequestError as err:
        print(f"[session] Không thể lấy homepage Facebook: {err}")
        return None
    except httpx.HTTPStatusError as err:
        print(f"[session] Lỗi HTTP khi lấy homepage: {err}")
        return None

    return _parse_home_response(response.text, setCookies)


async def dataGetHome(
    setCookies: str | None = None, storage: SessionStorage | None = None
) -> dict[str, Any] | None:
    setCookies = _resolve_cookies(setCookies, storage)
    if not setCookies:
        print("[session] Không có cookie để khởi tạo session.")
        return None

    try:
        response = await send_get_request_async(_build_home_request(setCookies))
        response.raise_for_status()
    except httpx.RequestError as err:
        print(f"[session] Không thể lấy homepage Facebook: {err}")
        return None
    except httpx.HTTPStatusError as err:
        print(f"[session] Lỗi HTTP khi lấy homepage: {err}")
        return None

    return _parse_home_response(response.text, setCookies)


def log(tag: str, message: str) -> None:
    line = f"[{datetime.now():%H:%M:%S}] [{tag}] {message}"
    try:
        print(line)
    except UnicodeEncodeError:
        enc = sys.stdout.encoding or "utf-8"
        print(line.encode(enc, errors="backslashreplace").decode(enc, errors="replace"))


def _deep_find(obj: Any, keys: set[str]) -> Any:
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in keys and v not in (None, "", [], {}):
                return v
        for v in obj.values():
            found = _deep_find(v, keys)
            if found is not None:
                return found
    elif isinstance(obj, (list, tuple)):
        for item in obj:
            found = _deep_find(item, keys)
            if found is not None:
                return found
    return None


async def _call_login_main(login_obj: Any) -> Any:
    main_fn = getattr(login_obj, "main", None)
    if main_fn is None:
        raise RuntimeError("login object không có method main()")
    if asyncio.iscoroutinefunction(main_fn):
        return await main_fn()
    result = await asyncio.to_thread(main_fn)
    if inspect.iscoroutine(result):
        result = await result
    return result


async def _build_datafb_from_cookie(cookie: str) -> dict[str, Any]:
    try:
        cfg: dict[str, Any] = {}
        if CONFIG_PATH.exists():
            try:
                with CONFIG_PATH.open("r", encoding="utf-8") as f:
                    cfg = json.load(f) or {}
            except Exception:
                cfg = {}
        cfg["cookies"] = cookie
        with CONFIG_PATH.open("w", encoding="utf-8") as f:
            json.dump(cfg, f, ensure_ascii=False, indent=2)
        try:
            set_private_file_permissions(CONFIG_PATH)
        except Exception:
            pass

        log("login", "→ dataGetHome(storage=...)")
        storage = FileSessionStorage(str(CONFIG_PATH), key="cookies")
        dataFB = await dataGetHome(storage=storage)
        if isinstance(dataFB, dict) and dataFB.get("fb_dtsg"):
            return dataFB
    except Exception as e:
        log("login", f"⚠️ {e}")

    raise RuntimeError("Không tạo được dataFB từ cookie")


async def _normalize_to_datafb(result: Any) -> dict[str, Any]:
    if not isinstance(result, dict):
        raise RuntimeError(f"Kết quả không phải dict: {type(result)}")

    log("login", f"response keys = {sorted(result)}")

    payload: Any = result
    if (
        "success" in result
        and isinstance(result["success"], dict)
        and len(result) <= 3
    ):
        payload = result["success"]

    if isinstance(payload.get("dataFB"), dict):
        return payload["dataFB"]
    if isinstance(result.get("dataFB"), dict):
        return result["dataFB"]
    if "fb_dtsg" in payload and "clientRevision" in payload:
        return payload

    nested = _deep_find(result, {"dataFB", "data_fb"})
    if isinstance(nested, dict) and "cookieFacebook" in nested:
        return nested

    cookie = _deep_find(
        result,
        {
            "cookie",
            "cookies",
            "cookieFacebook",
            "setCookies",
            "set_cookies",
            "session_cookies",
            "cookiesKey-ValueList",
        },
    )

    if isinstance(cookie, list) and cookie and isinstance(cookie[0], dict):
        cookie = "".join(
            f"{c.get('name')}={c.get('value')}; "
            for c in cookie
            if isinstance(c, dict) and c.get("name") and c.get("value")
        ).strip()

    if isinstance(cookie, dict):
        cookie = "; ".join(f"{k}={v}" for k, v in cookie.items() if v is not None)

    if isinstance(cookie, str) and cookie.strip():
        log("login", f"→ Trích được cookie ({len(cookie)} chars)")
        dataFB = await _build_datafb_from_cookie(cookie.strip())
        token = _deep_find(result, {"accessTokenFB", "access_token", "token"})
        if isinstance(token, str) and token:
            dataFB["access_token"] = token
        return dataFB

    if result.get("error"):
        raise RuntimeError(f"Login fail: {result['error']}")

    raise RuntimeError(f"Không nhận dạng response: keys={sorted(result)}")


async def login_with_credentials(
    email: str, password: str, otp: str | None
) -> dict[str, Any]:
    log("login", f"Đăng nhập: {email}")

    login_obj: Any = None
    if otp:
        try:
            login_obj = loginFacebook(
                email, password, AuthenticationGoogleCode=otp
            )
        except TypeError:
            login_obj = loginFacebook(email, password)
    else:
        login_obj = loginFacebook(email, password)

    raw_result = await _call_login_main(login_obj)
    dataFB = await _normalize_to_datafb(raw_result)

    if not dataFB.get("cookieFacebook"):
        raise RuntimeError("Không lấy được cookieFacebook")

    return dataFB


def push_to_esp32(dataFB: dict[str, Any]) -> bool:
    cookie = str(dataFB.get("cookieFacebook") or "").strip()
    dtsg = str(dataFB.get("fb_dtsg") or "").strip()
    rev = str(dataFB.get("clientRevision") or "").strip()
    jazoest = str(dataFB.get("jazoest") or "").strip()

    if not cookie:
        log("push", "❌ Cookie rỗng")
        return False

    log("push", f"→ POST {ESP32_URL}")
    log("push", f"  cookie  : {len(cookie)} chars")
    log("push", f"  fb_dtsg : {len(dtsg)} chars")
    log("push", f"  jazoest : {jazoest}")
    log("push", f"  rev     : {rev}")

    try:
        r = requests.post(
            ESP32_URL,
            data={
                "cookie": cookie,
                "dtsg": dtsg,
                "jazoest": jazoest,
                "rev": rev,
            },
            timeout=20,
        )
        log("push", f"✅ HTTP {r.status_code}")
        log("push", f"  body: {r.text[:200].strip()}")
        return r.status_code == 200

    except requests.exceptions.ConnectionError:
        log("push", "❌ Không kết nối được tới ESP32")
        log("push", "   → ESP32 đang ở SETUP PORTAL chưa?")
        log("push", "   → Cùng WiFi NAM MINH chưa?")
        log("push", "   → Thử: ping 192.168.1.11")
        return False
    except requests.exceptions.Timeout:
        log("push", "❌ Timeout")
        return False
    except Exception as e:
        log("push", f"❌ {e}")
        return False


async def main() -> None:
    log("boot", "=" * 60)
    log("boot", "LOGIN + PUSH COOKIE LÊN ESP32")
    log("boot", "=" * 60)

    dataFB = await login_with_credentials(EMAIL, PASSWORD, OTP)
    log("login", "✅ Đăng nhập thành công")

    print()
    print("=" * 70)
    print("  THÔNG TIN ĐĂNG NHẬP")
    print("=" * 70)
    print(f"  FacebookID     : {dataFB.get('FacebookID') or '?'}")
    print(f"  clientRevision : {dataFB.get('clientRevision') or '?'}")
    print(f"  jazoest        : {dataFB.get('jazoest') or '?'}")
    print(f"  fb_dtsg        : {str(dataFB.get('fb_dtsg') or '')[:60]}...")
    print(f"  cookie         : {len(str(dataFB.get('cookieFacebook') or ''))} chars")
    print("=" * 70)
    print()

    ok = push_to_esp32(dataFB)

    if ok:
        log("boot", "✅ Xong! ESP32 sẽ reboot trong ~2s")
    else:
        log("boot", "❌ Push thất bại")
        sys.exit(1)


def main_blocking() -> None:
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        log("bot", "Đã dừng")
    except Exception as e:
        log("boot", f"❌ {e}")
        raise SystemExit(1) from e


if __name__ == "__main__":
    main_blocking()
