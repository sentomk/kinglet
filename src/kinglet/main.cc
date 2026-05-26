#include "checker/type_checker.h"
#include "compiler/compiler.h"
#include "module/module_loader.h"
#include "lexer/scanner.h"
#include "lexer/token.h"
#include "parser/parser.h"
#include "vm/vm.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

enum class Mode {
  Run,
  Tokens,
  Ast,
  Bytecode,
  Repl,
};

void print_usage(std::ostream &out) {
  out << "usage: kinglet [--tokens | --ast | --bytecode | --repl] [file.kl]\n"
      << "\n"
      << "Reads Kinglet source from a .kl file, or stdin when file is omitted.\n"
      << "By default, compiles and runs main().\n";
}

std::string read_stdin() {
  std::ostringstream buffer;
  buffer << std::cin.rdbuf();
  return buffer.str();
}

bool read_file(std::string_view path, std::string *out) {
  std::ifstream file(std::string(path), std::ios::in | std::ios::binary);
  if (!file) {
    return false;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  *out = buffer.str();
  return true;
}

void print_escaped(std::ostream &out, std::string_view text) {
  for (char c : text) {
    switch (c) {
    case '\n':
      out << "\\n";
      break;
    case '\r':
      out << "\\r";
      break;
    case '\t':
      out << "\\t";
      break;
    default:
      out << c;
      break;
    }
  }
}

void print_token(const kinglet::Token &token) {
  std::cout << token.line << ':' << token.column << ' '
            << kinglet::token_type_name(token.type);

  if (!token.lexeme.empty()) {
    std::cout << " \"";
    print_escaped(std::cout, token.lexeme);
    std::cout << '"';
  }

  switch (token.type) {
  case kinglet::TokenType::INTEGER:
  case kinglet::TokenType::CHAR_LIT:
    std::cout << " = " << token.int_value;
    break;
  case kinglet::TokenType::FLOAT_LIT:
    std::cout << " = " << token.float_value;
    break;
  default:
    break;
  }

  std::cout << '\n';
}

} // namespace

int main(int argc, char **argv) {
  std::string input_path;
  Mode mode = Mode::Run;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--tokens") {
      mode = Mode::Tokens;
      continue;
    }
    if (arg == "--ast") {
      mode = Mode::Ast;
      continue;
    }
    if (arg == "--bytecode") {
      mode = Mode::Bytecode;
      continue;
    }
    if (arg == "--repl") {
      mode = Mode::Repl;
      continue;
    }
    if (!input_path.empty()) {
      std::cerr << "kinglet: expected at most one input file\n";
      print_usage(std::cerr);
      return 64;
    }
    input_path = std::string(arg);
  }

  std::string source;
  if (input_path.empty() && mode != Mode::Repl) {
    source = read_stdin();
  } else if (!input_path.empty()) {
    if (!read_file(input_path, &source)) {
      std::cerr << "kinglet: failed to read '" << input_path << "'\n";
      return 66;
    }
  }

  if (mode == Mode::Repl) {
    std::cout << "Kinglet REPL (type 'exit' to quit)\n";
    kinglet::Vm vm;
    while (true) {
      std::cout << "> " << std::flush;
      std::string line;
      if (!std::getline(std::cin, line) || line == "exit") {
        break;
      }
      if (line.empty()) {
        continue;
      }

      // Wrap in a main function if not already wrapped
      std::string wrapped_source;
      if (line.find("int main") == std::string::npos &&
          line.find("fn main") == std::string::npos) {
        std::string expr = line;
        while (!expr.empty() && expr.back() == ';') {
          expr.pop_back();
        }

        const char *return_types[] = {"int", "float", "string", "bool", "void"};
        bool found = false;
        for (const char *rt : return_types) {
          wrapped_source =
              "using io; " + std::string(rt) + " main() => " + expr + ";";
          kinglet::Scanner test_scanner(wrapped_source);
          auto test_tokens = test_scanner.scan_tokens();
          kinglet::Parser test_parser(test_tokens);
          auto test_result = test_parser.parse();
          if (!test_result.errors.empty()) continue;
          kinglet::TypeChecker test_checker;
          auto test_type = test_checker.check(*test_result.program);
          if (test_type.errors.empty()) {
            found = true;
            break;
          }
        }
        if (!found) {
          wrapped_source = "using io; int main() => " + expr + ";";
        }
      } else {
        wrapped_source = line;
      }

      kinglet::Scanner scanner(std::move(wrapped_source));
      const std::vector<kinglet::Token> tokens = scanner.scan_tokens();

      bool had_error = false;
      for (const kinglet::Token &token : tokens) {
        if (token.type == kinglet::TokenType::ERROR) {
          std::cerr << token.line << ':' << token.column << ": lexer error: "
                    << token.lexeme << '\n';
          had_error = true;
        }
      }
      if (had_error) {
        continue;
      }

      kinglet::Parser parser(tokens);
      kinglet::ParseResult result = parser.parse();
      for (const kinglet::ParseError &error : result.errors) {
        std::cerr << error.line << ':' << error.column << ": parse error: "
                  << error.message << '\n';
      }
      if (!result.errors.empty()) {
        continue;
      }

      kinglet::TypeChecker checker;
      kinglet::TypeCheckResult type_result = checker.check(*result.program);
      bool has_type_errors = false;
      for (const kinglet::TypeError &error : type_result.errors) {
        const char *label = error.severity == kinglet::DiagnosticSeverity::Warning ? "warning" : "error";
        std::cerr << error.location.line << ':' << error.location.column
                  << ": " << label << ": " << error.message << '\n';
        if (error.severity == kinglet::DiagnosticSeverity::Error) has_type_errors = true;
      }
      if (has_type_errors) {
        continue;
      }

      kinglet::Compiler compiler;
      kinglet::CompileResult compile_result = compiler.compile(*result.program);
      for (const kinglet::CompileError &error : compile_result.errors) {
        std::cerr << error.location.line << ':' << error.location.column
                  << ": compile error: " << error.message << '\n';
      }
      if (!compile_result.errors.empty()) {
        continue;
      }
      for (const kinglet::CompileWarning &warning : compile_result.warnings) {
        std::cerr << warning.location.line << ':' << warning.location.column
                  << ": warning: " << warning.message << '\n';
      }

      kinglet::VmResult vm_result = vm.run(compile_result.chunk);
      if (!vm_result.ok) {
        std::cerr << "runtime error: " << vm_result.error << '\n';
        continue;
      }

      if (vm_result.value.type != kinglet::ValueType::Null &&
          (vm_result.value.type != kinglet::ValueType::Int ||
           vm_result.value.int_value_storage != 0)) {
        std::cout << vm_result.value << '\n';
      }
    }
    return 0;
  }

  kinglet::Scanner scanner(std::move(source));
  const std::vector<kinglet::Token> tokens = scanner.scan_tokens();

  bool had_lexer_error = false;
  for (const kinglet::Token &token : tokens) {
    if (token.type == kinglet::TokenType::ERROR) {
      std::cerr << token.line << ':' << token.column << ": lexer error: "
                << token.lexeme << '\n';
      had_lexer_error = true;
    }
  }

  if (mode == Mode::Tokens) {
    for (const kinglet::Token &token : tokens) {
      print_token(token);
    }
    return had_lexer_error ? 65 : 0;
  }

  if (had_lexer_error) {
    return 65;
  }

  kinglet::Parser parser(tokens);
  kinglet::ParseResult result = parser.parse();
  for (const kinglet::ParseError &error : result.errors) {
    std::cerr << error.line << ':' << error.column << ": parse error: "
              << error.message << '\n';
  }

  if (!result.errors.empty()) {
    return 65;
  }

  if (mode == Mode::Ast) {
    result.program->print(std::cout);
    return 0;
  }

  std::string base_dir = ".";
  if (!input_path.empty()) {
    std::filesystem::path p(input_path);
    if (p.has_parent_path()) {
      base_dir = p.parent_path().string();
    }
  }
  kinglet::ModuleLoader module_loader(base_dir);
  if (!input_path.empty()) {
    module_loader.register_source_file(input_path);
  }

  kinglet::TypeChecker checker;
  checker.set_module_loader(&module_loader);
  kinglet::TypeCheckResult type_result = checker.check(*result.program);
  bool has_type_errors = false;
  for (const kinglet::TypeError &error : type_result.errors) {
    const char *label = error.severity == kinglet::DiagnosticSeverity::Warning ? "warning" : "error";
    std::cerr << error.location.line << ':' << error.location.column
              << ": " << label << ": " << error.message << '\n';
    if (error.severity == kinglet::DiagnosticSeverity::Error) has_type_errors = true;
  }

  if (has_type_errors) {
    return 65;
  }

  kinglet::Compiler compiler;
  compiler.set_module_loader(&module_loader);
  kinglet::CompileResult compile_result = compiler.compile(*result.program);
  for (const kinglet::CompileError &error : compile_result.errors) {
    std::cerr << error.location.line << ':' << error.location.column
              << ": compile error: " << error.message << '\n';
  }

  if (!compile_result.errors.empty()) {
    return 65;
  }

  for (const kinglet::CompileWarning &warning : compile_result.warnings) {
    std::cerr << warning.location.line << ':' << warning.location.column
              << ": warning: " << warning.message << '\n';
  }

  if (mode == Mode::Bytecode) {
    compile_result.chunk.disassemble(std::cout);
    return 0;
  }

  kinglet::Vm vm;
  kinglet::VmResult vm_result = vm.run(compile_result.chunk);
  if (!vm_result.ok) {
    std::cerr << "runtime error: " << vm_result.error << '\n';
    return 70;
  }

  return 0;
}
