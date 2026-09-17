# 🤖 ESP32 FB Chat Bot

Bot Facebook Messenger chạy **trực tiếp trên chip ESP32** (Arduino framework) — không cần server trung gian, không cần Node.js/Python chạy nền để vận hành chính. Bot đăng nhập bằng cookie Facebook, theo dõi tin nhắn mới trong một nhóm chat, và có thể tự động trả lời bằng AI. Kèm theo là một script Python hỗ trợ tự động làm mới cookie khi cookie cũ hết hạn.

> ⚠️ **Lưu ý quan trọng:** Repo này thao tác với Facebook thông qua các API nội bộ (không chính thức) của Messenger Web, bằng cookie tài khoản cá nhân. Việc này **vi phạm Điều khoản dịch vụ của Meta**, có thể khiến tài khoản bị khoá/hạn chế, và **không phải** là hình thức chatbot chính thức (Meta có Messenger Platform API dùng Page token cho mục đích đó). Hãy chỉ dùng trên tài khoản thử nghiệm, tự chịu trách nhiệm rủi ro, không dùng để spam hay xâm phạm quyền riêng tư người khác.

---

## Giới thiệu

ESP32 FB Chat Bot là một firmware chạy trên ESP32 (Arduino framework), đóng vai trò như một bot Messenger gọn nhẹ, không phụ thuộc server trung gian. Mục tiêu của dự án là chứng minh khả năng một vi điều khiển nhỏ vẫn có thể:

- Đăng nhập và duy trì phiên làm việc với Facebook bằng cookie, không cần OAuth chính thức.
- Theo dõi tin nhắn mới trong một cuộc trò chuyện cụ thể.
- Trả lời tự động bằng các mô hình AI khi được yêu cầu.
- Tương tác lại với tin nhắn (thả reaction), gửi tin nhắn văn bản.
- Cho phép cập nhật thông tin đăng nhập từ xa mà không cần nạp lại firmware.
- Hỗ trợ điều khiển, theo dõi và gỡ lỗi qua giao diện dòng lệnh Serial.

Đi kèm firmware là một script Python phụ trợ giúp tự động đăng nhập lại Facebook và cập nhật thông tin phiên mới cho thiết bị khi cần, giảm thao tác thủ công trong quá trình vận hành lâu dài.

## Vì sao dự án này thú vị

- Chạy hoàn toàn trên phần cứng nhúng, tối ưu để tiết kiệm bộ nhớ (RAM/heap) — phù hợp với giới hạn tài nguyên của ESP32.
- Tự xây dựng các cơ chế xử lý cấp thấp (giao tiếp mạng, xử lý dữ liệu) thay vì phụ thuộc hoàn toàn vào thư viện nặng.
- Hỗ trợ tích hợp linh hoạt với nhiều nhà cung cấp AI khác nhau.
- Có cơ chế lưu trữ cấu hình bền vững trên thiết bị, giúp bot hoạt động ổn định qua các lần khởi động lại.

## Trạng thái dự án

⚠️ Dự án đang trong quá trình **phát triển và nâng cấp liên tục**. Kiến trúc nội bộ, cách tổ chức mã nguồn, tính năng và cơ chế hoạt động có thể thay đổi thường xuyên giữa các phiên bản.

Thông tin chi tiết, cập nhật và nhật ký thay đổi (bao gồm cấu trúc thư mục, luồng hoạt động, hướng dẫn cấu hình/cài đặt cụ thể...) được ghi lại trong file [`LOG.md`](./LOG.md) — vui lòng tham khảo file đó để có thông tin luôn đúng với phiên bản mã nguồn hiện tại.

## Rủi ro & giới hạn sử dụng

Vì dự án tương tác với Facebook qua các API nội bộ không chính thức, người dùng cần lưu ý:

- Có nguy cơ vi phạm điều khoản dịch vụ của nền tảng, dẫn đến hạn chế hoặc khoá tài khoản.
- Cơ chế hoạt động phụ thuộc vào cấu trúc dữ liệu nội bộ của Facebook, có thể ngừng hoạt động bất cứ lúc nào nếu phía Facebook thay đổi.
- Chỉ nên sử dụng cho mục đích thử nghiệm, học tập cá nhân, trên tài khoản không quan trọng.

## Giấy phép

Repo phát hành theo giấy phép **GNU General Public License v2.0** — xem chi tiết trong file [`LICENSE`](./LICENSE).
