import * as vscode from 'vscode';
import * as path from 'path';

export function activate(context: vscode.ExtensionContext) {
    console.log('Bread Language extension is now active!');

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
                    'let', 'const', 'func', 'fn', 'if', 'else', 'while', 'for', 'in', 
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
                'func': 'Declares a function',
                'fn': 'Declares a function (alternative syntax)',
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
        signatureProvider
    );
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

    // Show output channel
    const outputChannel = vscode.window.createOutputChannel('Bread Build');
    outputChannel.show();
    outputChannel.clear();
    
    const action = shouldRun ? 'Building and running' : 'Building';
    outputChannel.appendLine(`${action} ${relativePath}...`);
    outputChannel.appendLine('');

    try {
        // Create terminal for build process
        const terminal = vscode.window.createTerminal({
            name: 'Bread Build',
            cwd: workspaceFolder.uri.fsPath
        });

        let buildCommand = '';
        
        // Check if breadlang compiler already exists
        const breadlangExists = await vscode.workspace.fs.stat(vscode.Uri.joinPath(workspaceFolder.uri, 'build', 'breadlang')).then(() => true, () => false);
        const hasCMake = await vscode.workspace.fs.stat(vscode.Uri.joinPath(workspaceFolder.uri, 'CMakeLists.txt')).then(() => true, () => false);
        
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
            buildCommand = `mkdir -p build && cd build && cmake .. && make breadlang && cd ..`;
            buildCommand += ` && ./build/breadlang -o ${fileName} "${relativePath}"`;
            if (shouldRun) {
                buildCommand += ` && ./${fileName}`;
            }
        } else {
            // Try to use system breadlang compiler
            outputChannel.appendLine('Trying system breadlang compiler...');
            buildCommand = `breadlang -o ${fileName} "${relativePath}"`;
            if (shouldRun) {
                buildCommand += ` && ./${fileName}`;
            }
        }

        terminal.sendText(buildCommand);
        terminal.show();

        outputChannel.appendLine(`Executing: ${buildCommand}`);
        outputChannel.appendLine('');
        outputChannel.appendLine('Build process started in terminal...');

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