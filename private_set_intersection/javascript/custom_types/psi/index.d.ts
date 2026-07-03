declare module 'psi_*' {
  type Status = {
    readonly Message: string
  }

  // Optional progress slot the engine writes its running processed-element count
  // into, so a caller can poll "n so far" against the known input size. The
  // native addon takes an Int32Array (typically SharedArrayBuffer-backed and
  // read from another thread); the WASM build takes a numeric byte offset into
  // its linear memory. Purely a UI hint -- never affects the returned value.
  type ProgressSlot = Int32Array | number

  type Result = {
    readonly Status?: Status
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    readonly Value: any
  }

  type CreateSetupMessageResult = Result & {
    readonly Value: string,
    readonly Permutation?: number[]
  }
  type CreateRequestResult = Result & {
    readonly Value: string
  }
  type ProcessRequestResult = Result & {
    readonly Value: string
  }
  type GetIntersectionResult = Result & {
    readonly Value: number[]
  }
  type GetAssociationTableResult = Result & {
    readonly Value: number[][]
  }
  type GetIntersectionSizeResult = Result & {
    readonly Value: number
  }
  type CreateClientResult = Result & {
    readonly Value: Client
  }
  type CreateServerResult = Result & {
    readonly Value: Server
  }

  export type Server = {
    readonly delete: () => void
    readonly CreateSetupMessage: (
      fpr: number,
      numClientInputs: number,
      inputs: readonly string[],
      dataStructure: DataStructure,
      includeSortingPermutation?: boolean,
      progress?: ProgressSlot
    ) => CreateSetupMessageResult
    readonly ProcessRequest: (
      clientRequest: Uint8Array,
      progress?: ProgressSlot
    ) => ProcessRequestResult
    readonly GetPrivateKeyBytes: () => Uint8Array
  }

  export type Client = {
    readonly delete: () => void
    readonly CreateRequest: (
      clientInputs: readonly string[],
      progress?: ProgressSlot
    ) => CreateRequestResult
    readonly GetIntersection: (
      serverSetup: Uint8Array,
      serverResponse: Uint8Array,
      progress?: ProgressSlot
    ) => GetIntersectionResult
    readonly GetAssociationTable: (
      serverSetup: Uint8Array,
      serverResponse: Uint8Array,
      progress?: ProgressSlot
    ) => GetAssociationTableResult
    readonly GetIntersectionSize: (
      serverSetup: Uint8Array,
      serverResponse: Uint8Array,
      progress?: ProgressSlot
    ) => GetIntersectionSizeResult
    readonly GetPrivateKeyBytes: () => Uint8Array
  }

  export type Package = {
    readonly version: () => string
  }

  export type DataStructure = {
    readonly Raw: any
    readonly GCS: any
    readonly BloomFilter: any
  }

  export type Library = {
    readonly delete: () => void
    readonly Package: Package
    readonly DataStructure: DataStructure
    readonly CreateSetupMessage: (
      fpr: number,
      numClientInputs: number,
      inputs: readonly string[],
      dataStructure: DataStructure
    ) => CreateSetupMessageResult
    readonly CreateRequest: (inputs: readonly string[]) => CreateRequestResult
    readonly ProcessRequest: (clientRequest: string) => ProcessRequestResult
    readonly GetIntersection: (
      setup: string,
      response: string
    ) => GetIntersectionResult
    readonly GetIntersectionSize: (
      setup: string,
      response: string
    ) => GetIntersectionSizeResult
    readonly GetPrivateKeyBytes: () => Uint8Array
    readonly PsiClient: {
      readonly CreateWithNewKey: (
        revealIntersection: boolean
      ) => CreateClientResult
      readonly CreateFromKey: (
        key: Uint8Array,
        revealIntersection: boolean
      ) => CreateClientResult
    }
    readonly PsiServer: {
      readonly CreateWithNewKey: (
        revealIntersection: boolean
      ) => CreateServerResult
      readonly CreateFromKey: (
        key: Uint8Array,
        revealIntersection: boolean
      ) => CreateServerResult
    }
  }

  export default function bin(): Promise<Library>
}
