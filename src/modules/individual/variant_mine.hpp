
#pragma once

// ============================================================================
// ВАРИАНТ 10. МОДЕЛИРОВАНИЕ РОСТА ИНВЕСТИЦИОННОГО ПОРТФЕЛЯ
// С КАПИТАЛИЗАЦИЕЙ.
// ============================================================================

#include <string>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <sstream>
#include <iomanip>
#include <locale>
#include <vector>

#include "../../json.hpp"

namespace TaskVariant {

    // Структура входных данных
    struct Input {
        double initial_investment = 0;
        double annual_return = 0;
        int investment_term_years = 0;

        bool initial_investment_valid = false;
        bool annual_return_valid = false;
        bool investment_term_years_valid = false;

        std::vector<std::string> parse_errors;
    };

    // Структура выходных данных
    struct Output {
        double final_amount = 0.0;
        double net_income = 0.0;
        double effective_yield = 0.0;
    };

    // Вспомогательная функция для чтения числового значения из JSON
    namespace detail {

        enum class Read {
            Ok,
            Missing,
            NotNumber
        };

        inline Read read_number(
            const std::string& body,
            const std::string& key,
            double& value
        ) {
            const std::string quoted = "\"" + key + "\"";

            size_t p = body.find(quoted);

            if (p == std::string::npos)
                return Read::Missing;

            p += quoted.size();

            while (p < body.size() &&
                   std::isspace((unsigned char)body[p]))
                ++p;

            if (p >= body.size() || body[p] != ':')
                return Read::NotNumber;

            ++p;

            while (p < body.size() &&
                   std::isspace((unsigned char)body[p]))
                ++p;

            if (p >= body.size())
                return Read::NotNumber;

            char c = body[p];

            if (!(c == '-' || (c >= '0' && c <= '9')))
                return Read::NotNumber;

            const char* begin = body.c_str() + p;
            char* end = nullptr;

            double v = std::strtod(begin, &end);

            if (end == begin || !std::isfinite(v))
                return Read::NotNumber;

            size_t q = p + (size_t)(end - begin);

            while (q < body.size() &&
                   std::isspace((unsigned char)body[q]))
                ++q;

            if (q >= body.size() ||
                (body[q] != ',' && body[q] != '}'))
                return Read::NotNumber;

            value = v;

            return Read::Ok;
        }
    }

    // Разбор входных данных JSON
    inline Input parse(const std::string& body) {
        Input in{};

        size_t first = body.find_first_not_of(" \t\r\n");
        size_t last = body.find_last_not_of(" \t\r\n");

        if (first == std::string::npos ||
            body[first] != '{' ||
            body[last] != '}') {
            in.parse_errors.push_back(
                "request body must be a JSON object"
            );
            return in;
        }

        auto take = [&](const std::string& key,
                        double& dst,
                        bool& valid,
                        std::vector<std::string>& errors) {

            auto result = detail::read_number(body, key, dst);

            if (result == detail::Read::Missing) {
                errors.push_back("missing field: " + key);
            }
            else if (result == detail::Read::NotNumber) {
                errors.push_back(key + " must be a number");
            }
            else {
                valid = true;
            }
        };

        // 1. Первоначальные инвестиции
        std::vector<std::string> initial_errors;

        take("initial_investment",
            in.initial_investment,
            in.initial_investment_valid,
            initial_errors);

        if (in.initial_investment_valid &&
            (in.initial_investment < 1000 ||
            in.initial_investment > 1000000000)) {
            initial_errors.push_back(
                "initial_investment must be in [1000, 1000000000]"
            );
        }

        // 2. Годовая доходность
        std::vector<std::string> return_errors;

        take("annual_return",
            in.annual_return,
            in.annual_return_valid,
            return_errors);

        if (in.annual_return_valid &&
            (in.annual_return < 0.1 ||
            in.annual_return > 100)) {
            return_errors.push_back(
                "annual_return must be in [0.1, 100]"
            );
        }

        // 3. Срок инвестирования
        std::vector<std::string> term_errors;
        double years = 0.0;
        bool years_valid = false;

        take("investment_term_years",
            years,
            years_valid,
            term_errors);

        if (years_valid) {
            if (std::floor(years) != years) {
                term_errors.push_back(
                    "investment_term_years must be an integer"
                );
            }
            else if (
                years < std::numeric_limits<int>::min() ||
                years > std::numeric_limits<int>::max()
            ) {
                term_errors.push_back(
                    "investment_term_years is out of integer range"
                );
            }
            else {
                in.investment_term_years =
                    static_cast<int>(years);

                in.investment_term_years_valid = true;

                if (in.investment_term_years < 1 ||
                    in.investment_term_years > 50) {
                    term_errors.push_back(
                        "investment_term_years must be in [1, 50]"
                    );
                }
            }
        }

        // Объединяем ошибки в порядке полей
        in.parse_errors.insert(
            in.parse_errors.end(),
            initial_errors.begin(), initial_errors.end()
        );

        in.parse_errors.insert(
            in.parse_errors.end(),
            return_errors.begin(), return_errors.end()
        );

        in.parse_errors.insert(
            in.parse_errors.end(),
            term_errors.begin(), term_errors.end()
        );

        return in;
    }


    // Проверка правильности входных данных
    inline std::vector<std::string> validate(const Input& input) {
        return input.parse_errors;
    }

    // Основные расчёты по формуле сложных процентов
    inline Output calculate(const Input& in) {
        Output out{};

        // Итоговая сумма с учётом капитализации
        out.final_amount =
            in.initial_investment *
            std::pow(
                1.0 + in.annual_return / 100.0,
                in.investment_term_years
            );

        // Чистый доход
        out.net_income =
            out.final_amount - in.initial_investment;

        // Эффективная годовая доходность
        out.effective_yield = in.annual_return;

        return out;
    }

    // Оформление денежной суммы с округлением до целого рубля
    inline std::string format_money(double value) {
        std::ostringstream stream;

        stream.imbue(std::locale::classic());

        stream << std::fixed << std::setprecision(0)
               << std::round(value);

        return stream.str();
    }

    // Оформление процентной ставки
    inline std::string format_percent(double value) {
        std::ostringstream stream;

        stream.imbue(std::locale::classic());

        stream << std::setprecision(15) << value;

        return stream.str();
    }

    // Формирование JSON-ответа
    inline std::string to_json(const Output& out) {
        JObj o;

        o.str(
            "final_amount",
            "Итоговая сумма: " +
            format_money(out.final_amount) + " руб."
        );

        o.str(
            "net_income",
            "Доход: " +
            format_money(out.net_income) + " руб."
        );

        o.str(
            "effective_yield",
            "Годовая доходность: " +
            format_percent(out.effective_yield) + "%"
        );

        return o.str();
    }

} // namespace TaskVariant
