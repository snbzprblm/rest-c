#pragma once
// ============================================================================
// МОДУЛЬ INDIVIDUAL: каркас роута.
// Не содержит математики — только вызовы функций варианта.
// ============================================================================

#include <string>
#include <vector>
#include <cmath>
#include "../../lib/httplib.h"
#include "../../database.hpp"
#include "../../json.hpp"
#include "../../routes/auth.hpp"

// ────────────────────────────────────────────────────────────────────────────
// ПОДКЛЮЧИТЕ СВОЙ ВАРИАНТ ЗДЕСЬ (замените одну строку).
// ────────────────────────────────────────────────────────────────────────────
//#include "variant_roi.hpp"
#include "variant_mine.hpp"

namespace Individual {
    using namespace std;
    using namespace httplib;

    // Структура одного автоматического теста
    struct TestCase {
        string group;
        string name;
        bool ok;
    };

    // Функция запуска 20 автоматических тестов
    inline vector<TestCase> run_variant_tests() {

        using namespace TaskVariant;

        // Сравнение чисел с учётом небольшой погрешности
        auto almost_equal = [](double a, double b) {
            return std::fabs(a - b) < 0.00001;
        };

        // Проверка правильных данных
        auto valid = [](const string& b) {
            return TaskVariant::validate(TaskVariant::parse(b)).empty();
        };

        // Проверка неправильных данных
        auto invalid = [](const string& b) {
            return !TaskVariant::validate(TaskVariant::parse(b)).empty();
        };

        // Создание JSON-запроса
        auto body = [](const string& investment,
                       const string& rate,
                       const string& years) {
            return "{\"initial_investment\":" + investment +
                   ",\"annual_return\":" + rate +
                   ",\"investment_term_years\":" + years + "}";
        };

        // Контрольные расчёты
        Output control = calculate(
            parse(body("100000", "8", "3"))
        );

        Output second = calculate(
            parse(body("50000", "12", "5"))
        );

        string json = to_json(control);

        return {

            // 1–5. Корректные данные

            {"correct",
             "Control example: final amount",
             almost_equal(control.final_amount, 125971.2)},

            {"correct",
             "Control example: net income",
             almost_equal(control.net_income, 25971.2)},

            {"correct",
             "Control example: effective yield",
             almost_equal(control.effective_yield, 8.0)},

            {"correct",
             "JSON response contains all output fields",
             json.find("\"final_amount\"") != string::npos &&
             json.find("\"net_income\"") != string::npos &&
             json.find("\"effective_yield\"") != string::npos},

            {"correct",
             "Second control example: final amount",
             almost_equal(second.final_amount, 88117.08416)},

            // 6–13. Граничные значения

            {"boundary",
             "Minimum investment accepted",
             valid(body("1000", "8", "1"))},

            {"boundary",
             "Maximum investment accepted",
             valid(body("1000000000", "8", "1"))},

            {"boundary",
             "Minimum annual return accepted",
             valid(body("100000", "0.1", "3"))},

            {"boundary",
             "Maximum annual return accepted",
             valid(body("100000", "100", "3"))},

            {"boundary",
             "Minimum investment term accepted",
             valid(body("100000", "8", "1"))},

            {"boundary",
             "Maximum investment term accepted",
             valid(body("100000", "8", "50"))},

            {"boundary",
             "Annual return below minimum rejected",
             invalid(body("100000", "0.09", "3"))},

            {"boundary",
             "Annual return above maximum rejected",
             invalid(body("100000", "100.01", "3"))},

            // 14–18. Неверные данные

            {"invalid",
             "Investment below minimum rejected",
             invalid(body("999", "8", "3"))},

            {"invalid",
             "Investment above maximum rejected",
             invalid(body("1000000001", "8", "3"))},

            {"invalid",
             "Negative annual return rejected",
             invalid(body("100000", "-5", "3"))},

            {"invalid",
             "Investment term above maximum rejected",
             invalid(body("100000", "8", "51"))},

            {"invalid",
             "Text instead of number rejected",
             invalid(body("100000", "\"8\"", "3"))},

            // 19–20. Неполные данные

            {"incomplete",
             "Missing investment term rejected",
             invalid("{\"initial_investment\":100000,\"annual_return\":8}")},

            {"invalid",
            "Multiple input errors detected in correct order",
            []() {
                auto errors = validate(parse(
                    "{\"annual_return\":\"abc\","
                    "\"investment_term_years\":51}"
                ));

                return errors.size() == 3 &&
                        errors[0] == "missing field: initial_investment" &&
                        errors[1] == "annual_return must be a number" &&
                        errors[2] == "investment_term_years must be in [1, 50]";
            }()
            }
        };
    }


    inline void init_routes(Server& svr) {

        // ─── POST /works/calc-individual ────────────────────────────────────
        svr.Post("/api/v1.1/works/calc-individual", [](const Request& req, Response& res) {
            string role, name;
            int uid = Auth::auth(req, role, name);
            if (!uid) { res.status = 401; return; }

            auto in = TaskVariant::parse(req.body);

            auto errors = TaskVariant::validate(in);
            if (!errors.empty()) {
                string error_json = "{\"errors\":[";

                for (size_t i = 0; i < errors.size(); ++i) {
                    if (i > 0) error_json += ",";
                    error_json += "\"" + errors[i] + "\"";
                }

                error_json += "]}";

                res.status = 400;
                res.set_content(error_json, "application/json");
                return;
            }

            auto out = TaskVariant::calculate(in);
            string result = TaskVariant::to_json(out);

            db_run("INSERT INTO works (user_id,datetime,input_json,result_json) "
                   "VALUES (?,?,?,?);",
                   {to_string(uid), now_iso(), req.body, result});
            res.set_content(result, "application/json");
        });

        // ─── GET /works/history-individual ──────────────────────────────────
        
        // История расчётов инвестиционного портфеля
        svr.Get("/api/v1.1/works/history-individual", [](const Request& req, Response& res) {
            string role, name;
            int uid = Auth::auth(req, role, name);

            if (!uid) {
                res.status = 401;
                return;
            }

            // Ограничиваем количество записей
            int limit = MAX_HISTORY_LIMIT;

            if (req.has_param("limit")) {
                try {
                    limit = max(1, min(
                        stoi(req.get_param_value("limit")),
                        MAX_HISTORY_LIMIT
                    ));
                }
                catch (...) {}
            }

            JArr arr;

            // Выбираем только инвестиционные расчёты текущего пользователя
            for (auto& r : db_all(
                "SELECT id,datetime,input_json,result_json FROM works "
                "WHERE user_id=? AND input_json LIKE ? "
                "ORDER BY id DESC LIMIT ?;",
                {
                    to_string(uid),
                    "%\"initial_investment\"%",
                    to_string(limit)
                })) {

                JObj o;
                o.int_("id", stoll(r[0]));
                o.str("datetime", r[1]);
                o.raw("input", r[2]);
                o.raw("result", r[3]);

                arr.add(o.str());
            }

            res.set_content(arr.str(), "application/json");
        });


        // ─── GET /admin/run-tests-individual ─────────────────────────────
        // Запуск 20 автоматических тестов (только admin и owner)
        svr.Get("/api/v1.1/admin/run-tests-individual",
            [](const Request& req, Response& res) {

                // Проверяем авторизацию
                string role, name;

                if (!Auth::auth(req, role, name)) {
                    res.status = 401;
                    return;
                }

                // Проверяем права администратора
                if (!Auth::admin(role)) {
                    res.status = 403;
                    return;
                }

                // Запускаем все тесты
                auto tests = run_variant_tests();

                int passed = 0;
                JArr cases;

                // Формируем результат каждого теста
                for (size_t i = 0; i < tests.size(); ++i) {

                    if (tests[i].ok)
                        passed++;

                    JObj o;

                    o.int_("tc_id", static_cast<long long>(i + 1));
                    o.str("group", tests[i].group);
                    o.str("name", tests[i].name);
                    o.str("status", tests[i].ok ? "PASSED" : "FAILED");

                    cases.add(o.str());
                }

                // Формируем общий отчёт
                int total = static_cast<int>(tests.size());

                JObj report;

                report.str(
                    "module",
                    "Variant 10: Investment Portfolio with Capitalization"
                );

                report.str(
                    "status",
                    passed == total ? "success" : "failed"
                );

                report.int_("total_tests", total);
                report.int_("passed", passed);
                report.int_("failed", total - passed);

                report.raw("test_cases", cases.str());

                res.set_content(report.str(), "application/json");
            });

    }
}