//------------------------------------------------------------------------------
// Tooling sample. Demonstrates:
//
// * How to write a simple source tool using libTooling.
// * How to use RecursiveASTVisitor to find interesting AST nodes.
// * How to use the Rewriter API to rewrite the source code.
//
// Eli Bendersky (eliben@gmail.com)
// This code is in the public domain
//------------------------------------------------------------------------------
#include <sstream>
#include <string>

#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"

#include "clang/Rewrite/Core/Rewriter.h"

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;

static llvm::cl::OptionCategory ToolingSampleCategory("Tooling Sample");

class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
	public:
		MyASTVisitor(Rewriter &R) : TheRewriter(R) {}

		bool VisitStmt(Stmt *s) {
			if (isa<IfStmt>(s)) {
				IfStmt *IfStatement = cast<IfStmt>(s);
				Stmt *Then = IfStatement->getThen();

				TheRewriter.InsertText(Then->getLocStart(), "// the 'if' part\n", true, true);

				Stmt *Else = IfStatement->getElse();
				if (Else)
					TheRewriter.InsertText(Else->getLocStart(), "// the 'else' part\n",true, true);
			}

			return true;
		}

		bool VisitFunctionDecl(FunctionDecl *f) {
			if (f->hasBody()) {
				Stmt *FuncBody = f->getBody();

				QualType QT = f->getReturnType();
				std::string TypeStr = QT.getAsString();

				DeclarationName DeclName = f->getNameInfo().getName();
				std::string FuncName = DeclName.getAsString();

				std::stringstream SSBefore;
				SSBefore << "// Begin function " << FuncName << " returning " << TypeStr
					<< "\n";
				SourceLocation ST = f->getSourceRange().getBegin();
				TheRewriter.InsertText(ST, SSBefore.str(), true, true);

				std::stringstream SSAfter;
				SSAfter << "\n// End function " << FuncName;
				ST = FuncBody->getLocEnd().getLocWithOffset(1);
				TheRewriter.InsertText(ST, SSAfter.str(), true, true);
			}

			return true;
		}

	private:
		Rewriter &TheRewriter;
};

class MyASTConsumer : public ASTConsumer {
public:
  MyASTConsumer(Rewriter &R) : Visitor(R) {}

  bool HandleTopLevelDecl(DeclGroupRef DR) override {
    for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
      Visitor.TraverseDecl(*b);
    }
    return true;
  }

private:
  MyASTVisitor Visitor;
};

class MyFrontendAction : public ASTFrontendAction {
public:
  MyFrontendAction() {}
  void EndSourceFileAction() override {
    SourceManager &SM = TheRewriter.getSourceMgr();
    llvm::errs() << "** EndSourceFileAction for: "
                 << SM.getFileEntryForID(SM.getMainFileID())->getName() << "\n";

    TheRewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
  }

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override {
    llvm::errs() << "** Creating AST consumer for: " << file << "\n";
    TheRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return llvm::make_unique<MyASTConsumer>(TheRewriter);
  }

private:
  Rewriter TheRewriter;
};

int main(int argc, const char **argv) {
  CommonOptionsParser op(argc, argv, ToolingSampleCategory);
  ClangTool Tool(op.getCompilations(), op.getSourcePathList());
  return Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
}
