import * as vscode from 'vscode';
import * as path from 'path';
import * as fs from 'fs';

let diagnosticCollection: vscode.DiagnosticCollection;

export function activate(context: vscode.ExtensionContext) {
    console.log('Bread Language extension is now active!');

    // Create diagnostic collection for error reporting
    diagnosticCollection = vscode.languages.createDiagnosticCollection('bread');
    context.subscriptions.push(diagnosticCollection);

    // Register document change listener for real-time error checking
    const documentChangeListener = vscode.workspace.onDidChangeTextDocument(event => {
        if (event.document.languageId === 'bread') {
            validateBreadDocument(event.document);
        }
    });

    // Register document open listener
    const documentOpenListener = vscode.workspace.onDidOpenTextDocument(document => {
        if (document.languageId === 'bread') {
            validateBreadDocument(document);
        }
    });

    // Validate all open Bread documents
    vscode.workspace.textDocuments.forEach(document => {
        if (document.languageId === 'bread') {
            validateBreadDocument(document);
        }
    });

    // Register build command
    const buildCommand = vscode.commands.registerCommand('bread.build', async (uri?: vscode.Uri) => {
        const activeEditor = vscode.window.activeTextEditor;
        const fileUri = uri || activeEditor?.document.uri;
        
        if (!fileUri) {
            vscode.window.showErrorMessage('No Bread file to build');
            return;
        }

        if (!fileUri.fsPath.endsWith('.bread')) {
            vscode.window.showErrorMessage('Selected file is not a Bread file');
            return;
        }

        await buildBreadFile(fileUri, false);
    });

    // Register build and run command
    const buildAndRunCommand = vscode.commands.registerCommand('bread.buildAndRun', async (uri?: vscode.Uri) => {
        const activeEditor = vscode.window.activeTextEditor;
        const fileUri = uri || activeEditor?.document.uri;
        
        if (!fileUri) {
            vscode.window.showErrorMessage('No Bread file to build and run');
            return;
        }

        if (!fileUri.fsPath.endsWith('.bread')) {
            vscode.window.showErrorMessage('Selected file is not a Bread file');
            return;
        }

        await buildBreadFile(fileUri, true);
    });

    // Register completion provider
    const completionProvider = vscode.languages.registerCompletionItemProvider(
        'bread',
        {
            provideCompletionItems(document: vscode.TextDocument, position: vscode.Position) {
                const completions: vscode.CompletionItem[] = [];

                // Keywords
                const keywords = [
                    'let', 'const', 'def', 'if', 'else', 'while', 'for', 'in', 
                    'return', 'break', 'continue', 'true', 'false', 'nil'
                ];
                keywords.forEach(keyword => {
                    const item = new vscode.CompletionItem(keyword, vscode.CompletionItemKind.Keyword);
                    item.detail = `Bread keyword: ${keyword}`;
                    completions.push(item);
                });

                // Types
                const types = ['Int', 'Bool', 'Float', 'Double', 'String'];
                types.forEach(type => {
                    const item = new vscode.CompletionItem(type, vscode.CompletionItemKind.Class);
                    item.detail = `Bread type: ${type}`;
                    completions.push(item);
                });

                // Built-in functions
                const builtins = [
                    { name: 'print', detail: 'print(value) - Print a value to console' },
                    { name: 'len', detail: 'len(collection) -> Int - Get length of collection' },
                    { name: 'type', detail: 'type(value) -> String - Get type of value' },
                    { name: 'str', detail: 'str(value) -> String - Convert value to string' },
                    { name: 'int', detail: 'int(value) -> Int - Convert value to integer' },
                    { name: 'float', detail: 'float(value) -> Double - Convert value to float' },
                    { name: 'range', detail: 'range(count) - Create a range for iteration' },
                    { name: 'append', detail: 'array.append(value) - Add value to array' },
                    { name: 'toString', detail: 'value.toString() -> String - Convert to string' }
                ];
                builtins.forEach(builtin => {
                    const item = new vscode.CompletionItem(builtin.name, vscode.CompletionItemKind.Function);
                    item.detail = builtin.detail;
                    completions.push(item);
                });

                // Properties
                const properties = [
                    { name: 'length', detail: 'Get length of string, array, or dictionary' }
                ];
                properties.forEach(prop => {
                    const item = new vscode.CompletionItem(prop.name, vscode.CompletionItemKind.Property);
                    item.detail = prop.detail;
                    completions.push(item);
                });

                return completions;
            }
        },
        '.' // Trigger completion on dot
    );

    // Register hover provider
    const hoverProvider = vscode.languages.registerHoverProvider('bread', {
        provideHover(document, position) {
            const range = document.getWordRangeAtPosition(position);
            const word = document.getText(range);

            const hoverInfo: { [key: string]: string } = {
                'let': 'Declares a mutable variable',
                'const': 'Declares an immutable variable',
                'def': 'Declares a function',
                'if': 'Conditional statement',
                'else': 'Alternative branch for if statement',
                'while': 'Loop that continues while condition is true',
                'for': 'Iterate over a collection',
                'in': 'Used in for-in loops',
                'return': 'Return a value from a function',
                'break': 'Exit from a loop',
                'continue': 'Skip to next iteration of loop',
                'true': 'Boolean true value',
                'false': 'Boolean false value',
                'nil': 'Null/empty value',
                'Int': 'Integer type (32-bit)',
                'Bool': 'Boolean type (true/false)',
                'Float': 'Floating-point number type',
                'Double': 'Double-precision floating-point type',
                'String': 'Text string type',
                'print': 'Built-in function to print values to console',
                'len': 'Built-in function to get length of collections',
                'type': 'Built-in function to get type of a value',
                'str': 'Built-in function to convert value to string',
                'int': 'Built-in function to convert value to integer',
                'float': 'Built-in function to convert value to float',
                'range': 'Built-in function to create ranges for iteration',
                'append': 'Method to add elements to arrays',
                'toString': 'Method to convert values to strings',
                'length': 'Property to get length of strings, arrays, or dictionaries'
            };

            if (hoverInfo[word]) {
                return new vscode.Hover(hoverInfo[word]);
            }

            return null;
        }
    });

    // Register signature help provider
    const signatureProvider = vscode.languages.registerSignatureHelpProvider(
        'bread',
        {
            provideSignatureHelp(document, position) {
                const signatures: { [key: string]: vscode.SignatureInformation } = {
                    'print': new vscode.SignatureInformation(
                        'print(value)',
                        'Print a value to the console'
                    ),
                    'len': new vscode.SignatureInformation(
                        'len(collection) -> Int',
                        'Get the length of a string, array, or dictionary'
                    ),
                    'type': new vscode.SignatureInformation(
                        'type(value) -> String',
                        'Get the type name of a value'
                    ),
                    'str': new vscode.SignatureInformation(
                        'str(value) -> String',
                        'Convert a value to its string representation'
                    ),
                    'int': new vscode.SignatureInformation(
                        'int(value) -> Int',
                        'Convert a value to an integer'
                    ),
                    'float': new vscode.SignatureInformation(
                        'float(value) -> Double',
                        'Convert a value to a floating-point number'
                    ),
                    'range': new vscode.SignatureInformation(
                        'range(count) -> Iterable',
                        'Create a range from 0 to count-1 for iteration'
                    )
                };

                // Simple function name detection
                const line = document.lineAt(position.line).text;
                const beforeCursor = line.substring(0, position.character);
                const match = beforeCursor.match(/(\w+)\s*\($/);
                
                if (match) {
                    const funcName = match[1];
                    if (signatures[funcName]) {
                        const help = new vscode.SignatureHelp();
                        help.signatures = [signatures[funcName]];
                        help.activeSignature = 0;
                        help.activeParameter = 0;
                        return help;
                    }
                }

                return null;
            }
        },
        '(', ','
    );

    context.subscriptions.push(
        buildCommand,
        buildAndRunCommand,
        completionProvider, 
        hoverProvider, 
        signatureProvider,
        documentChangeListener,
        documentOpenListener
    );
}

function validateBreadDocument(document: vscode.TextDocument) {
    const diagnostics: vscode.Diagnostic[] = [];
    const text = document.getText();
    const lines = text.split('\n');

    for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        const lineNumber = i;

        // Check for common syntax errors
        
        // Check for old 'func' keyword usage
        const funcMatch = line.match(/\b(func|fn)\s+/);
        if (funcMatch) {
            const startPos = line.indexOf(funcMatch[0]);
            const range = new vscode.Range(
                lineNumber, startPos,
                lineNumber, startPos + funcMatch[1].length
            );
            const diagnostic = new vscode.Diagnostic(
                range,
                `Use 'def' instead of '${funcMatch[1]}' to declare functions`,
                vscode.DiagnosticSeverity.Warning
            );
            diagnostic.code = 'deprecated-keyword';
            diagnostics.push(diagnostic);
        }

        // Check for missing type annotations
        const letMatch = line.match(/\blet\s+(\w+)\s*=\s*/);
        if (letMatch && !line.includes(':')) {
            const startPos = line.indexOf(letMatch[0]);
            const range = new vscode.Range(
                lineNumber, startPos,
                lineNumber, line.length
            );
            const diagnostic = new vscode.Diagnostic(
                range,
                'Consider adding type annotation for better code clarity',
                vscode.DiagnosticSeverity.Information
            );
            diagnostic.code = 'missing-type-annotation';
            diagnostics.push(diagnostic);
        }

        // Skip bracket matching - VS Code handles this natively with language configuration

        // Check for missing semicolons (if required by style)
        if (line.trim().match(/^(let|const)\s+.*[^;{]$/) && !line.includes('{')) {
            const range = new vscode.Range(lineNumber, line.length - 1, lineNumber, line.length);
            const diagnostic = new vscode.Diagnostic(
                range,
                'Consider adding semicolon for consistency',
                vscode.DiagnosticSeverity.Hint
            );
            diagnostic.code = 'missing-semicolon';
            diagnostics.push(diagnostic);
        }
    }

    diagnosticCollection.set(document.uri, diagnostics);
}

async function buildBreadFile(fileUri: vscode.Uri, shouldRun: boolean = false) {
    const workspaceFolder = vscode.workspace.getWorkspaceFolder(fileUri);
    if (!workspaceFolder) {
        vscode.window.showErrorMessage('File must be in a workspace to build');
        return;
    }

    const fileName = path.basename(fileUri.fsPath, '.bread');
    const relativePath = path.relative(workspaceFolder.uri.fsPath, fileUri.fsPath);
    
    // Save the file first
    const document = await vscode.workspace.openTextDocument(fileUri);
    if (document.isDirty) {
        await document.save();
    }

    // Clear previous diagnostics
    diagnosticCollection.clear();

    // Show output channel
    const outputChannel = vscode.window.createOutputChannel('Bread Build');
    outputChannel.show();
    outputChannel.clear();
    
    const action = shouldRun ? 'Building and running' : 'Building';
    outputChannel.appendLine(`${action} ${relativePath}...`);
    outputChannel.appendLine('');

    try {
        // Check if breadlang compiler exists
        const breadlangPath = path.join(workspaceFolder.uri.fsPath, 'build', 'breadlang');
        const breadlangExists = fs.existsSync(breadlangPath);
        const hasCMake = fs.existsSync(path.join(workspaceFolder.uri.fsPath, 'CMakeLists.txt'));
        
        if (!breadlangExists && !hasCMake) {
            const message = 'Bread compiler not found. Please build the project first or install breadlang globally.';
            outputChannel.appendLine(message);
            vscode.window.showErrorMessage(message);
            return;
        }

        // Create terminal for build process
        const terminal = vscode.window.createTerminal({
            name: 'Bread Build',
            cwd: workspaceFolder.uri.fsPath
        });

        let buildCommand = '';
        
        if (breadlangExists) {
            // Use existing breadlang compiler
            outputChannel.appendLine('Using existing breadlang compiler...');
            buildCommand = `./build/breadlang -o ${fileName} "${relativePath}"`;
            if (shouldRun) {
                buildCommand += ` && ./${fileName}`;
            }
        } else if (hasCMake) {
            // Build the breadlang compiler first, then use it to compile the .bread file
            outputChannel.appendLine('Building breadlang compiler first...');
            buildCommand = `cmake --build build || (mkdir -p build && cd build && cmake .. && make breadlang && cd ..)`;
            buildCommand += ` && ./build/breadlang -o ${fileName} "${relativePath}"`;
            if (shouldRun) {
                buildCommand += ` && ./${fileName}`;
            }
        }

        terminal.sendText(buildCommand);
        terminal.show();

        outputChannel.appendLine(`Executing: ${buildCommand}`);
        outputChannel.appendLine('');
        outputChannel.appendLine('Build process started in terminal...');
        outputChannel.appendLine('Check the terminal for detailed output and any error messages.');

        // Show success message
        const message = shouldRun ? 
            `Building and running ${fileName}...` : 
            `Building ${fileName}...`;
        vscode.window.showInformationMessage(message);

    } catch (error) {
        const errorMessage = `Build failed: ${error}`;
        outputChannel.appendLine(errorMessage);
        vscode.window.showErrorMessage(errorMessage);
    }
}

export function deactivate() {}